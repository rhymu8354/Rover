/**
 * @file main.cpp
 *
 * This module holds the main() function, which is the entrypoint
 * to the program.
 *
 * © 2018 by Richard Walters
 */

#include "TimeKeeper.hpp"

#include <Http/Client.hpp>
#include <HttpNetworkTransport/HttpClientNetworkTransport.hpp>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <SystemAbstractions/DiagnosticsStreamReporter.hpp>

namespace {

    /**
     * This flag indicates whether or not the web client should shut down.
     */
    bool shutDown = false;

    /**
     * This contains variables set through the operating system environment
     * or the command-line arguments.
     */
    struct Environment {
        /**
         * This is the URL of the resource to fetch.
         */
        std::string url;
    };

    /**
     * This function is set up to be called when the SIGINT signal is
     * received by the program.  It just sets the "shutDown" flag
     * and relies on the program to be polling the flag to detect
     * when it's been set.
     *
     * @param[in] sig
     *     This is the signal for which this function was called.
     */
    void InterruptHandler(int) {
        shutDown = true;
    }

    /**
     * This function updates the program environment to incorporate
     * any applicable command-line arguments.
     *
     * @param[in] argc
     *     This is the number of command-line arguments given to the program.
     *
     * @param[in] argv
     *     This is the array of command-line arguments given to the program.
     *
     * @param[in,out] environment
     *     This is the environment to update.
     *
     * @return
     *     An indication of whether or not the function succeeded is returned.
     */
    bool ProcessCommandLineArguments(
        int argc,
        char* argv[],
        Environment& environment
    ) {
        size_t state = 0;
        for (int i = 1; i < argc; ++i) {
            const std::string arg(argv[i]);
            switch (state) {
                case 0: { // next argument
                    if (!environment.url.empty()) {
                        fprintf(stderr, "error: multiple URLs given\n");
                        return false;
                    }
                    environment.url = arg;
                    state = 0;
                } break;
            }
        }
        return true;
    }

    /**
     * This function starts the client with the given transport layer.
     *
     * @param[in,out] client
     *     This is the client to start.
     *
     * @param[in] environment
     *     This contains variables set through the operating system
     *     environment or the command-line arguments.
     *
     * @param[in] diagnosticMessageDelegate
     *     This is the function to call to publish any diagnostic messages.
     */
    void StartClient(
        Http::Client& client,
        const Environment& environment,
        SystemAbstractions::DiagnosticsSender::DiagnosticMessageDelegate diagnosticMessageDelegate
    ) {
        auto transport = std::make_shared< HttpNetworkTransport::HttpClientNetworkTransport >();
        transport->SubscribeToDiagnostics(diagnosticMessageDelegate, 0);
        Http::Client::MobilizationDependencies deps;
        deps.transport = transport;
        deps.timeKeeper = std::make_shared< TimeKeeper >();
        client.Mobilize(deps);
    }

    /**
     * This function is called from the main function, once the web client
     * is up and running.  It fetches the given resource and generates a
     * report to the standard output stream.  It returns once the report
     * has been generated, or if the user has signaled to end the program.
     *
     * @param[in,out] client
     *     This is the client to use to fetch the resource.
     *
     * @param[in] environment
     *     This contains variables set through the operating system
     *     environment or the command-line arguments.
     *
     * @param[in] diagnosticMessageDelegate
     *     This is the function to call to publish any diagnostic messages.
     */
    void FetchResourceAndReport(
        Http::Client& client,
        const Environment& environment,
        SystemAbstractions::DiagnosticsSender::DiagnosticMessageDelegate diagnosticMessageDelegate
    ) {
        while (!shutDown) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    }

}

/**
 * This function is the entrypoint of the program.
 * It just sets up the web client, using it to fetch a resource
 * and generate a report.  It registers the SIGINT signal to know
 * when the web client should be shut down early.
 *
 * The program is terminated after either a report is generated
 * or the SIGINT signal is caught.
 *
 * @param[in] argc
 *     This is the number of command-line arguments given to the program.
 *
 * @param[in] argv
 *     This is the array of command-line arguments given to the program.
 */
int main(int argc, char* argv[]) {
#ifdef _WIN32
    //_crtBreakAlloc = 18;
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif /* _WIN32 */
    const auto previousInterruptHandler = signal(SIGINT, InterruptHandler);
    Environment environment;
    if (!ProcessCommandLineArguments(argc, argv, environment)) {
        return EXIT_FAILURE;
    }
    Http::Client client;
    (void)setbuf(stdout, NULL);
    const auto diagnosticsPublisher = SystemAbstractions::DiagnosticsStreamReporter(stderr, stderr);
    const auto diagnosticsSubscription = client.SubscribeToDiagnostics(diagnosticsPublisher);
    StartClient(client, environment, diagnosticsPublisher);
    diagnosticsPublisher("Rover", 3, "Web client up and running.");
    FetchResourceAndReport(client, environment, diagnosticsPublisher);
    (void)signal(SIGINT, previousInterruptHandler);
    diagnosticsPublisher("Rover", 3, "Exiting...");
    return EXIT_SUCCESS;
}
