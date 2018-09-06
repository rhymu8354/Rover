#ifndef TLS_DECORATOR_HPP
#define TLS_DECORATOR_HPP

/**
 * @file TlsDecorator.hpp
 *
 * This module declares the TlsDecorator implementation.
 *
 * © 2018 by Richard Walters
 */

#include <Http/ClientTransport.hpp>
#include <memory>

/**
 * This is a decorator for Http::ClientTransport which passes all
 * data through a TLS layer.
 */
class TlsDecorator
    : public Http::ClientTransport
{
    // Lifecycle Methods
public:
    ~TlsDecorator() noexcept;
    TlsDecorator(const TlsDecorator&) = delete;
    TlsDecorator(TlsDecorator&&) noexcept = delete;
    TlsDecorator& operator=(const TlsDecorator&) = delete;
    TlsDecorator& operator=(TlsDecorator&&) noexcept = delete;

    // Public Methods
public:
    /**
     * This is the constructor of the class.
     */
    TlsDecorator();

    /**
     * This method sets up the decorator to decorate the
     * given upper-level client transport.
     *
     * @param[in] upperLayer
     *     This is the upper-level client transport to decorate.
     */
    void Configure(std::shared_ptr< Http::ClientTransport > upperLayer);

    // Http::ClientTransport
public:
    virtual std::shared_ptr< Http::Connection > Connect(
        const std::string& hostNameOrAddress,
        uint16_t port,
        Http::Connection::DataReceivedDelegate dataReceivedDelegate,
        Http::Connection::BrokenDelegate brokenDelegate
    ) override;

    // Private properties
private:
    /**
     * This is the type of structure that contains the private
     * properties of the instance.  It is defined in the implementation
     * and declared here to ensure that it is scoped inside the class.
     */
    struct Impl;

    /**
     * This contains the private properties of the instance.
     */
    std::unique_ptr< Impl > impl_;
};

#endif /* TLS_DECORATOR_HPP */
