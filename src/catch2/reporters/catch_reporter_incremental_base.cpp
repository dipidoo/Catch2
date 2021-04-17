
//              Copyright Catch2 Authors
// Distributed under the Boost Software License, Version 1.0.
//   (See accompanying file LICENSE_1_0.txt or copy at
//        https://www.boost.org/LICENSE_1_0.txt)

// SPDX-License-Identifier: BSL-1.0
#include <algorithm>
#include <cassert>
#include <catch2/catch_test_spec.hpp>
#include <catch2/reporters/catch_reporter_helpers.hpp>
#include <catch2/reporters/catch_reporter_incremental_base.hpp>

namespace Catch {
    IncrementalSectionTraversal::IncrementalSectionTraversal():
        testRunInfo{ "" },
        testGroupInfo{ "", 0, 0 },
        startTime{ std::chrono::system_clock::now() },
        finishTime{ std::chrono::system_clock::now() } {}

    // If we're using the fancy redirect behavior, we want to ensure that we
    // flush any pending data from the sink to the traversal's stream to ensure
    // we don't emit things out of order.
    std::ostringstream& IncrementalSectionTraversal::
        IncrementalSectionTraversal::getFlushedStdOut() {
#ifdef CATCH_CONFIG_EXPERIMENTAL_REDIRECT
        if ( m_stdOutSourceSink ) {
            m_stdOutStream << m_stdOutSourceSink->getContents();
            m_stdOutSourceSink->reset();
        }
#endif
        return m_stdOutStream;
    }

    std::ostringstream& IncrementalSectionTraversal::
        IncrementalSectionTraversal::getFlushedStdErr() {
#ifdef CATCH_CONFIG_EXPERIMENTAL_REDIRECT
        if ( m_stdErrSourceSink ) {
            m_stdErrStream << m_stdErrSourceSink->getContents();
            m_stdErrSourceSink->reset();
        }
#endif
        return m_stdErrStream;
    }

    // Record an assertion associated with this traversal.
    //
    // If we've already recorded a fatal signal, *do not* use the backing
    // collections, as memory can't be trusted for push_back in a terminal
    // state. Instead, just record the file/line info for future (non-heap)
    // serialization.
    //
    // Delayed expansion of an expression doesn't work (i.e. you can't hold on
    // to an AssertionStats& forever and then expect to have the backing data
    // available) so we record the expansion in tandem with the other info.
    void IncrementalSectionTraversal::addAssertion(
        const Catch::AssertionStats& assertion ) {
        if ( !fatalSignalName.empty() ) {
            auto lineInfo = assertion.assertionResult.getSourceInfo();
            fatalSignalSourceInfo = { lineInfo.file, lineInfo.line };
        } else {
            auto expanded = assertion.assertionResult.getExpandedExpression();
            allAssertionsWithExpansions.push_back( { assertion, expanded } );
            auto& outStream = getFlushedStdOut();
            for ( const auto& info : assertion.infoMessages ) {
                outStream << "INFO: " << info.message << '\n';
            }
        }
    }

    // As a full-depth traversal of section hierarchy proceeds, it
    // accumulates SectionInfo "on the way down" and SectionStats "on the
    // way back up." Once the number of complementary SectionStats is
    // equivalent to the number of SectionInfo we've processed, the
    // full-depth traversal is complete.
    bool IncrementalSectionTraversal::isComplete() const {
        return !allSectionInfo.empty() &&
               allSectionInfo.size() == allSectionStats.size();
    }

    bool IncrementalSectionTraversal::isOk() const {
        return isComplete() && fatalSignalName.empty() &&
               std::all_of( allAssertionsWithExpansions.begin(),
                            allAssertionsWithExpansions.end(),
                            []( const std::pair<Catch::AssertionStats,
                                                std::string>& entry ) {
                                return entry.first.assertionResult.isOk();
                            } );
    }

#ifdef CATCH_CONFIG_EXPERIMENTAL_REDIRECT
    void IncrementalSectionTraversal::setRedirectSinksFromConfig(
        const IConfig* config ) {
        m_stdOutSourceSink.reset( config->standardOutputRedirect() );
        m_stdErrSourceSink.reset( config->standardErrorRedirect() );
    }

    void IncrementalSectionTraversal::setRedirectSinksFromPredecessor(
        IncrementalSectionTraversal& predecessor ) {
        m_stdOutSourceSink.reset( predecessor.m_stdOutSourceSink.release() );
        m_stdErrSourceSink.reset( predecessor.m_stdErrSourceSink.release() );
    }
#endif

    const std::vector<IncrementalReporterBase::SectionTraversalRef>
    IncrementalReporterBase::getTraversals() {
        std::vector<SectionTraversalRef> result;
        for ( auto& traversal : m_completedTraversals ) {
            result.push_back( std::ref( traversal ) );
        }
        if ( !m_currentTraversal.allSectionInfo.empty() ) {
            result.push_back( std::ref( m_currentTraversal ) );
        }
        return result;
    }

    IncrementalReporterBase::IncrementalReporterBase(
        const ReporterConfig& config ):
        IStreamingReporter{ config.fullConfig() },
        m_currentTestRunInfo{ "" },
        m_currentTestGroupInfo{ "", 0, 0 },
        m_outputStreamRef{ config.stream() } {
#ifdef CATCH_CONFIG_EXPERIMENTAL_REDIRECT
        m_currentTraversal.setRedirectSinksFromConfig( m_config );
#endif
    }

    bool IncrementalReporterBase::incrementalOutputSupported() const {
        return !m_config->outputFilename().empty();
    }

    bool IncrementalReporterBase::perSectionRedirectedOutputSupported() const {
#ifdef CATCH_CONFIG_EXPERIMENTAL_REDIRECT
        return m_config->standardOutputRedirect() ||
               m_config->standardErrorRedirect();
#else
        return false;
#endif
    }

    void IncrementalReporterBase::resetIncrementalOutput() {
        m_incrementalOutputStream.reset(
            Catch::makeStream( m_config->outputFilename() ) );
        m_outputStreamRef = m_incrementalOutputStream->stream();
    }

    // When used in conjunction with the appropriate capture capability and the
    // --standard-out-redirect-file and/or --standard-err-redirect file options,
    // incremental reporters support separating redirected output on a
    // per-section-traversal basis. If the capability isn't present or the
    // options simply weren't specified, we'll default to the standard
    // per-test-case output redirection.
    bool IncrementalReporterBase::isRedirectingOutputPerTraversal() const {
#ifdef CATCH_CONFIG_EXPERIMENTAL_CAPTURE
        return m_config->standardOutputRedirect() ||
               m_config->standardErrorRedirect();
#endif
        return false;
    }

    void
    IncrementalReporterBase::testRunStarting( const TestRunInfo& testRunInfo ) {
        m_currentTestRunInfo = testRunInfo;
    }

    void IncrementalReporterBase::testGroupStarting(
        const GroupInfo& testGroupInfo ) {
        m_currentTestGroupInfo = testGroupInfo;
    }

    void IncrementalReporterBase::testCaseStarting(
        const TestCaseInfo& testCaseInfo ) {
        m_currentTestTags = testCaseInfo.tags;
    }

    void
    IncrementalReporterBase::testCaseEnded( const TestCaseStats& testStats ) {
        auto& traversal =
            m_completedTraversals.empty() ||
                    !m_currentTraversal.fatalSignalName.empty()
                ? m_currentTraversal
                : m_completedTraversals[m_completedTraversals.size() - 1];
        traversal.getFlushedStdOut() << testStats.stdOut;
        traversal.getFlushedStdErr() << testStats.stdErr;
    }

    void
    IncrementalReporterBase::sectionStarting( SectionInfo const& sectionInfo ) {
        if ( m_currentTraversal.allSectionInfo.empty() ) {
            m_currentTraversal.startTime = std::chrono::system_clock::now();
            m_currentTraversal.testRunInfo = m_currentTestRunInfo;
            m_currentTraversal.testGroupInfo = m_currentTestGroupInfo;
            m_currentTraversal.testTags = m_currentTestTags;
            sectionTraversalStarting( getTraversals() );
        }
        m_currentTraversal.allSectionInfo.push_back( sectionInfo );
    }

    bool IncrementalReporterBase::assertionEnded(
        const AssertionStats& assertionStats ) {
        if ( !assertionStats.assertionResult.isOk() ) {
            m_currentTraversal.addAssertion( assertionStats );
        }
        return true;
    }

    void
    IncrementalReporterBase::sectionEnded( const SectionStats& sectionStats ) {
        m_currentTraversal.allSectionStats.push_back( sectionStats );

        if ( m_currentTraversal.isComplete() ) {
            // Ensure any redirected output makes it to the traversal
            (void)m_currentTraversal.getFlushedStdOut();
            (void)m_currentTraversal.getFlushedStdErr();

            m_currentTraversal.finishTime = std::chrono::system_clock::now();
            m_completedTraversals.push_back( std::move( m_currentTraversal ) );
#ifdef CATCH_CONFIG_EXPERIMENTAL_REDIRECT
            auto& lastTraversal =
                m_completedTraversals[m_completedTraversals.size() - 1];
            m_currentTraversal.setRedirectSinksFromPredecessor( lastTraversal );
#endif
            sectionTraversalEnded( getTraversals() );
        }
    }

    void
    IncrementalReporterBase::fatalErrorEncountered( StringRef signalName ) {
        m_currentTraversal.fatalSignalName = signalName.data();
    }

    void IncrementalReporterBase::listReporters(
        std::vector<ReporterDescription> const& descriptions ) {
        defaultListReporters(
            m_outputStreamRef.get(), descriptions, m_config->verbosity() );
    }

    void IncrementalReporterBase::listTests(
        std::vector<TestCaseHandle> const& tests ) {
        defaultListTests( m_outputStreamRef.get(),
                          tests,
                          m_config->hasTestFilters(),
                          m_config->verbosity() );
    }

    void IncrementalReporterBase::listTags( std::vector<TagInfo> const& tags ) {
        defaultListTags(
            m_outputStreamRef.get(), tags, m_config->hasTestFilters() );
    }

} // end namespace Catch
