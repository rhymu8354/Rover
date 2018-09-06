/**
 * @file TlsDecorator.cpp
 *
 * This module contains the implementations of the TlsDecorator class.
 *
 * © 2018 by Richard Walters
 */

#include "TlsDecorator.hpp"

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
    return impl_->upperLayer->Connect(
        hostNameOrAddress,
        port,
        dataReceivedDelegate,
        brokenDelegate
    );
}
