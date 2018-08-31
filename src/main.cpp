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
#include <Http/Request.hpp>
#include <HttpNetworkTransport/HttpClientNetworkTransport.hpp>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <SystemAbstractions/DiagnosticsStreamReporter.hpp>

namespace {

    /**
     * This function prints to the standard error stream information
     * about how to use this program.
     */
    void PrintUsageInformation() {
        fprintf(
            stderr,
            (
                "Usage: Rover URL\n"
                "\n"
                "Fetch a web resource and output its contents to the standard output stream.\n"
                "\n"
                "  URL     Locator for resource to fetch\n"
            )
        );
    }

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
     * @param[in] diagnosticMessageDelegate
     *     This is the function to call to publish any diagnostic messages.
     *
     * @return
     *     An indication of whether or not the function succeeded is returned.
     */
    bool ProcessCommandLineArguments(
        int argc,
        char* argv[],
        Environment& environment,
        SystemAbstractions::DiagnosticsSender::DiagnosticMessageDelegate diagnosticMessageDelegate
    ) {
        size_t state = 0;
        for (int i = 1; i < argc; ++i) {
            const std::string arg(argv[i]);
            switch (state) {
                case 0: { // next argument
                    if (!environment.url.empty()) {
                        diagnosticMessageDelegate(
                            "Rover",
                            SystemAbstractions::DiagnosticsSender::Levels::ERROR,
                            "multiple URLs given"
                        );
                        return false;
                    }
                    environment.url = arg;
                    state = 0;
                } break;
            }
        }
        if (environment.url.empty()) {
            diagnosticMessageDelegate(
                "Rover",
                SystemAbstractions::DiagnosticsSender::Levels::ERROR,
                "no URL given"
            );
            return false;
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
        Http::Request request;
        request.method = "GET";
        if (!request.target.ParseFromString(environment.url)) {
            diagnosticMessageDelegate(
                "Rover",
                SystemAbstractions::DiagnosticsSender::Levels::ERROR,
                "bad URL given"
            );
            return;
        }
        diagnosticMessageDelegate(
            "Rover",
            1,
            "Fetching '" + request.target.GenerateString() + "'..."
        );
        const auto transaction = client.Request(
            request,
            false
        );
        while (!shutDown) {
            if (transaction->AwaitCompletion(std::chrono::milliseconds(250))) {
                switch (transaction->state) {
                    case Http::Client::Transaction::State::Completed: {
                        (void)printf(
                            (
                                "Response: %u %s\n"
                                "Headers: ---------------\n"
                            ),
                            transaction->response.statusCode,
                            transaction->response.reasonPhrase.c_str()
                        );
                        for (const auto& header: transaction->response.headers.GetAll()) {
                            (void)printf(
                                "%s: %s\n",
                                ((std::string)header.name).c_str(),
                                header.value.c_str()
                            );
                        }
                        (void)printf("------------------------\n");
                        if (!transaction->response.body.empty()) {
                            (void)fwrite(
                                transaction->response.body.c_str(),
                                transaction->response.body.length(),
                                1,
                                stdout
                            );
                            (void)fwrite("\n", 1, 1, stdout);
                        }
                    } break;

                    case Http::Client::Transaction::State::UnableToConnect: {
                        diagnosticMessageDelegate(
                            "Rover",
                            SystemAbstractions::DiagnosticsSender::Levels::ERROR,
                            "unable to connect"
                        );
                    } break;

                    case Http::Client::Transaction::State::Broken: {
                        diagnosticMessageDelegate(
                            "Rover",
                            SystemAbstractions::DiagnosticsSender::Levels::ERROR,
                            "connection broken by server"
                        );
                    } break;

                    case Http::Client::Transaction::State::Timeout: {
                        diagnosticMessageDelegate(
                            "Rover",
                            SystemAbstractions::DiagnosticsSender::Levels::ERROR,
                            "timeout waiting for response"
                        );
                    } break;
                }
                return;
            }
        }
        diagnosticMessageDelegate(
            "Rover",
            SystemAbstractions::DiagnosticsSender::Levels::WARNING,
            "Fetch Canceled"
        );
    }

    /**
     * This function stops the client.
     *
     * @param[in,out] client
     *     This is the client to stop.
     */
    void StopClient(Http::Client& client) {
        client.Demobilize();
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
    (void)setbuf(stdout, NULL);
    const auto diagnosticsPublisher = SystemAbstractions::DiagnosticsStreamReporter(stderr, stderr);
    if (!ProcessCommandLineArguments(argc, argv, environment, diagnosticsPublisher)) {
        PrintUsageInformation();
        return EXIT_FAILURE;
    }
    Http::Client client;
    const auto diagnosticsSubscription = client.SubscribeToDiagnostics(diagnosticsPublisher);
    StartClient(client, environment, diagnosticsPublisher);
    diagnosticsPublisher("Rover", 3, "Web client up and running.");
    FetchResourceAndReport(client, environment, diagnosticsPublisher);
    (void)signal(SIGINT, previousInterruptHandler);
    diagnosticsPublisher("Rover", 3, "Exiting...");
    StopClient(client);
    return EXIT_SUCCESS;
}
