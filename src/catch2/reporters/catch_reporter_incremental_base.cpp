
//              Copyright Catch2 Authors
// Distributed under the Boost Software License, Version 1.0.
//   (See accompanying file LICENSE_1_0.txt or copy at
//        https://www.boost.org/LICENSE_1_0.txt)

// SPDX-License-Identifier: BSL-1.0
#include <catch2/reporters/catch_reporter_incremental_base.hpp>
#include <catch2/reporters/catch_reporter_helpers.hpp>
#include <catch2/catch_test_spec.hpp>

#include <algorithm>
#include <cassert>

namespace Catch {
    IncrementalSectionTraversal::IncrementalSectionTraversal()
        : testRunInfo{ "" }
        , testGroupInfo{ "", 0, 0 }
        , startTime{ std::chrono::system_clock::now() }
        , finishTime{ std::chrono::system_clock::now() }
    {}

    // Record an assertion associated with this traversal. If we've already recorded a fatal signal, *do not* use the
    // backing collections, as memory can't be trusted for push_back in a terminal state. Instead, just record the
    // file/line info for future (non-heap) serialization.
    void IncrementalSectionTraversal::addAssertion( const Catch::AssertionStats& assertion ) {
        if ( !fatalSignalName.empty() ) {
            auto lineInfo = assertion.assertionResult.getSourceInfo();
            fatalSignalSourceInfo = { lineInfo.file, lineInfo.line };
        } else {
            auto expanded = assertion.assertionResult.getExpandedExpression();
            allAssertionsWithExpansions.push_back( { assertion, expanded } );
            for ( const auto& info : assertion.infoMessages ) {
                stdOutStream << "INFO: " << info.message << '\n';
            }
        }
    }

    bool IncrementalSectionTraversal::isComplete() const {
        // As a full-depth traversal of section hierarchy proceeds, it accumulates SectionInfo
        // "on the way down" and SectionStats "on the way back up." Once the number of
        // complementary SectionStats is equivalent to the number of SectionInfo we've processed,
        // the full-depth traversal is complete.
        return !allSectionInfo.empty() && allSectionInfo.size() == allSectionStats.size();
    }

    bool IncrementalSectionTraversal::isOk() const {
        return isComplete() && fatalSignalName.empty() && std::all_of(
            allAssertionsWithExpansions.begin(),
            allAssertionsWithExpansions.end(),
            []( const std::pair<Catch::AssertionStats, std::string>& entry ) {
                return entry.first.assertionResult.isOk();
            } );
    }

    const std::vector<IncrementalReporterBase::SectionTraversalRef> IncrementalReporterBase::getTraversals() {
        std::vector<SectionTraversalRef> result;
        for ( auto& traversal : m_completedTraversals ) {
            result.push_back( std::ref( traversal ) );
        }
        if ( !m_currentTraversal.allSectionInfo.empty() ) {
            result.push_back( std::ref( m_currentTraversal ) );
        }
        return result;
    }

    IncrementalReporterBase::IncrementalReporterBase( const ReporterConfig& config )
        : IStreamingReporter{ config.fullConfig() }
        , m_currentTestRunInfo{ "" }
        , m_currentTestGroupInfo{ "", 0, 0 } 
        , m_outputStreamRef{ config.stream() }
    {}

    bool IncrementalReporterBase::incrementalOutputSupported() const {
        return !m_config->outputFilename().empty();
    }

    void IncrementalReporterBase::resetIncrementalOutput() {
        m_incrementalOutputStream.reset( Catch::makeStream( m_config->outputFilename() ) );
        m_outputStreamRef = m_incrementalOutputStream->stream();
    }

    void IncrementalReporterBase::testRunStarting( const TestRunInfo& testRunInfo ) {
        m_currentTestRunInfo = testRunInfo;
    }

    void IncrementalReporterBase::testGroupStarting(const GroupInfo& testGroupInfo) {
        m_currentTestGroupInfo = testGroupInfo;
    }

    void IncrementalReporterBase::testCaseStarting( const TestCaseInfo& testCaseInfo ) {
        m_currentTestTags = testCaseInfo.tags;
    }
    
    void IncrementalReporterBase::sectionStarting( SectionInfo const& sectionInfo ) {
        if ( m_currentTraversal.allSectionInfo.empty() ) {
            m_currentTraversal.startTime = std::chrono::system_clock::now();
            m_currentTraversal.testRunInfo = m_currentTestRunInfo;
            m_currentTraversal.testGroupInfo = m_currentTestGroupInfo;
            m_currentTraversal.testTags = m_currentTestTags;
            sectionTraversalStarting( getTraversals() );
        }
        m_currentTraversal.allSectionInfo.push_back( sectionInfo );
    }

    bool IncrementalReporterBase::assertionEnded(const AssertionStats& assertionStats) {
        if ( !assertionStats.assertionResult.isOk() ) {
            m_currentTraversal.addAssertion( assertionStats );
        }
        return true;
    }

    void IncrementalReporterBase::sectionEnded( const SectionStats& sectionStats ) {
        m_currentTraversal.allSectionStats.push_back( sectionStats );

        if ( m_currentTraversal.isComplete() ) {
#ifdef CATCH_CONFIG_NEW_CAPTURE
            // If we're using output redirect sinks, this is the cue that everything we currently have in the redirect
            // sink should be associated with the test we're wrapping up. Record it and reset the sinks for future
            // tests.
            if ( m_config->standardOutputRedirect() ) {
                m_currentTraversal.stdOutStream << m_config->standardOutputRedirect()->getContents();
                m_config->standardOutputRedirect()->reset();
            }
            if ( m_config->standardErrorRedirect() ) {
                m_currentTraversal.stdErrStream << m_config->standardErrorRedirect()->getContents();
                m_config->standardErrorRedirect()->reset();
            }
#endif
            m_currentTraversal.finishTime = std::chrono::system_clock::now();
            m_completedTraversals.push_back( std::move( m_currentTraversal ) );
            sectionTraversalEnded( getTraversals() );
        }
    }

    void IncrementalReporterBase::fatalErrorEncountered( StringRef signalName ) {
        m_currentTraversal.fatalSignalName += signalName;
    }

    void IncrementalReporterBase::listReporters(std::vector<ReporterDescription> const& descriptions) {
        defaultListReporters( m_outputStreamRef.get(), descriptions, m_config->verbosity() );
    }

    void IncrementalReporterBase::listTests( std::vector<TestCaseHandle> const& tests ) {
        defaultListTests( m_outputStreamRef.get(), tests, m_config->hasTestFilters(), m_config->verbosity() );
    }

    void IncrementalReporterBase::listTags( std::vector<TagInfo> const& tags ) {
        defaultListTags( m_outputStreamRef.get(), tags, m_config->hasTestFilters() );
    }

} // end namespace Catch
