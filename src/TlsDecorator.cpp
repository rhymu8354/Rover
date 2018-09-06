/**
 * @file TlsDecorator.cpp
 *
 * This module contains the implementations of the TlsDecorator class.
 *
 * © 2018 by Richard Walters
 */

#include "TlsDecorator.hpp"

#include <algorithm>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <stdio.h>
#include <tls.h>
#include <thread>

namespace {

    /**
     * This is the number of bytes to allocate for receiving decrypted
     * data from the TLS layer.
     */
    constexpr size_t DECRYPTED_BUFFER_SIZE = 65536;

    /**
     * This is a decorator for Http::Connection which passes all
     * data through a TLS layer.
     */
    class TlsConnectionDecorator
        : public Http::Connection
    {
        // Lifecycle management
    public:
        ~TlsConnectionDecorator() noexcept {
            printf("admNuke\n");
            if (worker_.joinable()) {
                {
                    std::lock_guard< decltype(mutex_) > lock(mutex_);
                    stopWorker_ = true;
                    wakeCondition_.notify_all();
                }
                worker_.join();
            }
        }
        TlsConnectionDecorator(const TlsConnectionDecorator&) = delete;
        TlsConnectionDecorator(TlsConnectionDecorator&&) noexcept = delete;
        TlsConnectionDecorator& operator=(const TlsConnectionDecorator&) = delete;
        TlsConnectionDecorator& operator=(TlsConnectionDecorator&&) noexcept = delete;

        // Public Methods
    public:
        /**
         * This is the default constructor.
         */
        TlsConnectionDecorator() {
            receiveBufferDecrypted_.reserve(DECRYPTED_BUFFER_SIZE);
        }

        /**
         * This method is called when secure data comes in from the TLS layer.
         *
         * @param[in] data
         *     This is the data that was received from the remote peer.
         */
        void SecureDataReceived(const std::vector< uint8_t >& data){
            std::lock_guard< decltype(mutex_) > lock(mutex_);
            printf("received secure data (%zu more bytes, %zu total)\n", data.size(), data.size() + receiveBufferSecure_.size());
            receiveBufferSecure_.insert(
                receiveBufferSecure_.end(),
                data.begin(),
                data.end()
            );
            wakeCondition_.notify_all();
        };

        /**
         * This method is called when the upper-layer connection is broken.
         */
        void ConnectionBroken() {
            printf("other end broke\n");
            {
                std::lock_guard< decltype(mutex_) > lock(mutex_);
                open_ = false;
                wakeCondition_.notify_all();
            }
            if (
                receiveBufferSecure_.empty()
                && (brokenDelegate_ != nullptr)
            ) {
                brokenDelegate_(false);
            }
        }

        /**
         * This method establishes a TLS connection and sets up the decorator
         * so that all data passes through the TLS connection.
         *
         * @param[in] upperLayer
         *     This is the upper-level client connection to decorate.
         *
         * @param[in] dataReceivedDelegate
         *     This is the delegate to call whenever data is recevied
         *     from the remote peer.
         *
         * @param[in] dataReceivedDelegate
         *     This is the delegate to call whenever the connection
         *     has been broken.
         *
         * @param[in] serverName
         *     This is the name of the server with which to connect
         *     as a TLS client.
         *
         * @return
         *     An indication of whether or not the function succeeded
         *     is returned.
         */
        bool Connect(
            std::shared_ptr< Http::Connection > upperLayer,
            Http::Connection::DataReceivedDelegate dataReceivedDelegate,
            Http::Connection::BrokenDelegate brokenDelegate,
            const std::string& serverName
        ) {
            if (worker_.joinable()) {
                return false;
            }
            upperLayer_ = upperLayer;
            dataReceivedDelegate_ = dataReceivedDelegate;
            brokenDelegate_ = brokenDelegate;
            tlsConfig_ = decltype(tlsConfig_)(
                tls_config_new(),
                [](tls_config* p){
                    tls_config_free(p);
                }
            );
            tlsImpl_ = decltype(tlsImpl_)(
                tls_client(),
                [](tls* p) {
                    tls_close(p);
                    tls_free(p);
                }
            );
            printf("tls_configure()...\n");

            // ----------------------------------
            // I don't know about this, but it was in the example....
            tls_config_insecure_noverifycert(tlsConfig_.get());
            tls_config_insecure_noverifyname(tlsConfig_.get());
            // ----------------------------------

            tls_config_set_protocols(tlsConfig_.get(), TLS_PROTOCOLS_DEFAULT);

            if (tls_configure(tlsImpl_.get(), tlsConfig_.get()) != 0) {
                return false;
            }
            printf("tls_connect_cbs()...\n");
            if (
                tls_connect_cbs(
                    tlsImpl_.get(),
                    [](struct tls *_ctx, void *_buf, size_t _buflen, void *_cb_arg){
                        const auto self = (TlsConnectionDecorator*)_cb_arg;
                        std::lock_guard< decltype(self->mutex_) > lock(self->mutex_);
                        printf("_read_cb(%zu) -- %zu is available\n", _buflen, self->receiveBufferSecure_.size());
                        const auto amt = std::min(_buflen, self->receiveBufferSecure_.size());
                        if (
                            (amt == 0)
                            && self->open_
                        ) {
                            return (ssize_t)TLS_WANT_POLLIN;
                        }
                        self->canWrite_ = true;
                        (void)memcpy(_buf, self->receiveBufferSecure_.data(), amt);
                        if (amt == self->receiveBufferSecure_.size()) {
                            self->receiveBufferSecure_.clear();
                        } else {
                            self->receiveBufferSecure_.erase(
                                self->receiveBufferSecure_.begin(),
                                self->receiveBufferSecure_.begin() + amt
                            );
                        }
                        return (ssize_t)amt;
                    },
                    [](struct tls *_ctx, const void *_buf, size_t _buflen, void *_cb_arg){
                        printf("_write_cb(%zu)\n", _buflen);
                        const auto self = (TlsConnectionDecorator*)_cb_arg;
                        std::lock_guard< decltype(self->mutex_) > lock(self->mutex_);
                        if (self->open_) {
                            const auto bufBytes = (const uint8_t*)_buf;
                            self->upperLayer_->SendData(
                                std::vector< uint8_t >(bufBytes, bufBytes + _buflen)
                            );
                        }
                        return (ssize_t)_buflen;
                    },
                    this,
                    serverName.c_str()
                ) != 0
            ) {
                return false;
            }
            printf("TLS connected... starting worker\n");
            stopWorker_ = false;
            worker_ = std::thread(&TlsConnectionDecorator::Worker, this);
            return true;
        }

        // Http::Connection
    public:
        virtual std::string GetPeerAddress() override {
            return upperLayer_->GetPeerAddress();
        }

        virtual std::string GetPeerId() override {
            return upperLayer_->GetPeerId();
        }

        virtual void SetDataReceivedDelegate(DataReceivedDelegate dataReceivedDelegate) {
        }

        virtual void SetBrokenDelegate(BrokenDelegate brokenDelegate) {
        }

        virtual void SendData(const std::vector< uint8_t >& data) {
            printf("queueing %zu to send to TLS\n", data.size());
            std::unique_lock< decltype(mutex_) > lock(mutex_);
            sendBuffer_.insert(
                sendBuffer_.end(),
                data.begin(),
                data.end()
            );
            wakeCondition_.notify_all();
        }

        virtual void Break(bool clean) {
            printf("breaking our end\n");
            upperLayer_->Break(false);
        }

        // Private Methods
    private:
        /**
         * This method runs in a separate thread, performing all
         * I/O with the TLS layer.
         */
        void Worker() {
            printf("worker: starting\n");
            std::unique_lock< decltype(mutex_) > lock(mutex_);
            bool tryRead = true;
            while (!stopWorker_) {
                if (
                    !sendBuffer_.empty()
                    && canWrite_
                    && open_
                ) {
                    printf("tls_write (%zu)\n", sendBuffer_.size());
                    const auto amount = tls_write(
                        tlsImpl_.get(),
                        sendBuffer_.data(),
                        sendBuffer_.size()
                    );
                    if (amount == TLS_WANT_POLLIN) {
                        // Can't write any more until we read some more...
                        printf("tls_write returned: TLS_WANT_POLLIN\n");
                        canWrite_ = false;
                    } else if (amount < 0) {
                        printf("tls_write returned %d -- ERROR?\n", (int)amount);
                        upperLayer_->Break(false);
                    } else {
                        printf("tls_write returned %zd\n", (size_t)amount);
                        if ((size_t)amount == sendBuffer_.size()) {
                            sendBuffer_.clear();
                        } else {
                            sendBuffer_.erase(
                                sendBuffer_.begin(),
                                sendBuffer_.begin() + (size_t)amount
                            );
                        }
                    }
                }
                if (
                    !receiveBufferSecure_.empty()
                    || tryRead
                ) {
                    printf("tls_read (%zu)\n", DECRYPTED_BUFFER_SIZE);
                    tryRead = true;
                    receiveBufferDecrypted_.resize(DECRYPTED_BUFFER_SIZE);
                    const auto amount = tls_read(
                        tlsImpl_.get(),
                        receiveBufferDecrypted_.data(),
                        receiveBufferDecrypted_.size()
                    );
                    if (amount == TLS_WANT_POLLIN) {
                        printf("tls_read returned: TLS_WANT_POLLIN\n");
                        // Can't read any more because we're out of data.
                    } else if (amount == TLS_WANT_POLLOUT) {
                        // Can't read any more until we write some more...
                        // (I think we shouldn't ever get here, but let's see...
                        printf("TLS_WANT_POLLOUT\n");
                    } else if (amount < 0) {
                        const auto tlsErrorMessage = tls_error(tlsImpl_.get());
                        printf("tls_read returned %d -- ERROR? tls_error says: \"%s\"\n", (int)amount, tlsErrorMessage);
                        upperLayer_->Break(false);
                    } else if (amount > 0) {
                        printf("tls_read returned %zd\n", (size_t)amount);
                        receiveBufferDecrypted_.resize((size_t)amount);
                        if (dataReceivedDelegate_ != nullptr) {
                            lock.unlock();
                            dataReceivedDelegate_(receiveBufferDecrypted_);
                            lock.lock();
                        }
                        if (
                            receiveBufferSecure_.empty()
                            && !open_
                        ) {
                            if (brokenDelegate_ != nullptr) {
                                brokenDelegate_(false);
                            }
                        }
                    } else {
                        tryRead = false;
                    }
                }
                wakeCondition_.wait(
                    lock,
                    [this]{
                        return (
                            stopWorker_
                            || !receiveBufferSecure_.empty()
                            || (
                                !sendBuffer_.empty()
                                && canWrite_
                            )
                        );
                    }
                );
                printf("worker: wake up\n");
            }
            printf("worker: stopping\n");
        }

        // Private Properties
    private:
        /**
         * This is the upper-level client connection to decorate.
         */
        std::shared_ptr< Http::Connection > upperLayer_;

        /**
         * This is the method to call to deliver data received
         * and decrypted from the TLS layer.
         */
        DataReceivedDelegate dataReceivedDelegate_;

        /**
         * This is the method to call whenever the underlying
         * connection has been broken.
         */
        BrokenDelegate brokenDelegate_;

        /**
         * This implements the TLS layer.
         */
        std::unique_ptr< tls, std::function< void(tls*) > > tlsImpl_;

        /**
         * This is used to configure the TLS layer.
         */
        std::unique_ptr< tls_config, std::function< void(tls_config*) > > tlsConfig_;

        /**
         * This is used to synchronize access to the state of this object.
         */
        std::recursive_mutex mutex_;

        /**
         * This is used to alert any threads that might be waiting on
         * changes to the state of this object.
         */
        std::condition_variable_any wakeCondition_;

        /**
         * This holds data sent from the upper-level client connection
         * before it's written to the TLS layer.
         */
        std::vector< uint8_t > sendBuffer_;

        /**
         * This holds data received from the upper-level client connection
         * before it's read by the TLS layer.
         */
        std::vector< uint8_t > receiveBufferSecure_;

        /**
         * This holds data received from the TLS layer, to be delivered
         * to the data received delegate.
         */
        std::vector< uint8_t > receiveBufferDecrypted_;

        /**
         * This flag keeps track of whether or not the upper-level client
         * connection is still open.
         */
        bool open_ = true;

        /**
         * This flag indicates whether or not we should attempt to write
         * data to the TLS layer.  It's cleared if tls_write returns
         * TLS_WANT_POLLIN, and set again when the read callback is able
         * to deliver more data to the TLS layer.
         */
        bool canWrite_ = true;

        /**
         * This thread performs all TLS read/write asynchronously.
         */
        std::thread worker_;

        /**
         * This flag is set whenever the worker thread should stop.
         */
        bool stopWorker_ = true;
    };

}

/**
 * This contains the private properties of a TlsDecorator class instance.
 */
struct TlsDecorator::Impl {
    /**
     * This is the upper-level client transport to decorate.
     */
    std::shared_ptr< Http::ClientTransport > upperLayer;
};

TlsDecorator::~TlsDecorator() noexcept = default;

TlsDecorator::TlsDecorator()
    : impl_(new Impl())
{
}

void TlsDecorator::Configure(std::shared_ptr< Http::ClientTransport > upperLayer) {
    impl_->upperLayer = upperLayer;
}

std::shared_ptr< Http::Connection > TlsDecorator::Connect(
    const std::string& hostNameOrAddress,
    uint16_t port,
    Http::Connection::DataReceivedDelegate dataReceivedDelegate,
    Http::Connection::BrokenDelegate brokenDelegate
) {
    const auto connectionDecorator = std::make_shared< TlsConnectionDecorator >();
    TlsConnectionDecorator* connectionDecoratorRaw = connectionDecorator.get();
    if (
        !connectionDecorator->Connect(
            impl_->upperLayer->Connect(
                hostNameOrAddress,
                port,
                std::bind(
                    &TlsConnectionDecorator::SecureDataReceived,
                    connectionDecoratorRaw,
                    std::placeholders::_1
                ),
                std::bind(
                    &TlsConnectionDecorator::ConnectionBroken,
                    connectionDecoratorRaw
                )
            ),
            dataReceivedDelegate,
            brokenDelegate,
            hostNameOrAddress
        )
    ) {
        return nullptr;
    }
    return connectionDecorator;
}
