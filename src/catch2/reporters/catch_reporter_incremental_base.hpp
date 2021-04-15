
//              Copyright Catch2 Authors
// Distributed under the Boost Software License, Version 1.0.
//   (See accompanying file LICENSE_1_0.txt or copy at
//        https://www.boost.org/LICENSE_1_0.txt)

// SPDX-License-Identifier: BSL-1.0
#ifndef CATCH_REPORTER_INCREMENTAL_BASE_HPP_INCLUDED
#define CATCH_REPORTER_INCREMENTAL_BASE_HPP_INCLUDED

#include <catch2/interfaces/catch_interfaces_reporter.hpp>
#include <catch2/internal/catch_unique_ptr.hpp>
#include <catch2/catch_timer.hpp>
#include <catch2/catch_test_case_info.hpp>
#include <catch2/internal/catch_stream.hpp>
#include <iosfwd>
#include <string>
#include <sstream>
#include <vector>
#include <functional>

namespace Catch {

    // A "section traversal" represents a single, depth-first execution path within a test case.
    struct IncrementalSectionTraversal {
        IncrementalSectionTraversal();
        IncrementalSectionTraversal( IncrementalSectionTraversal const& ) = delete;
        IncrementalSectionTraversal& operator=( const IncrementalSectionTraversal&) = delete;
        IncrementalSectionTraversal( IncrementalSectionTraversal&& ) = default;
        IncrementalSectionTraversal& operator=( IncrementalSectionTraversal&& ) = default;

        void addAssertion( const Catch::AssertionStats& assertion );

        bool isComplete() const;
        bool isOk() const;

        std::vector<Catch::SectionInfo> allSectionInfo;
        std::vector<Catch::SectionStats> allSectionStats;
        std::vector<std::pair<Catch::AssertionStats, std::string>> allAssertionsWithExpansions;

        std::string fatalSignalName;
        std::pair<std::string, size_t> fatalSignalSourceInfo;

        Catch::TestRunInfo testRunInfo;
        Catch::GroupInfo testGroupInfo;
        std::vector<Catch::Tag> testTags;

        std::chrono::system_clock::time_point startTime;
        std::chrono::system_clock::time_point finishTime;

        std::ostringstream stdOutStream;
        std::ostringstream stdErrStream;
    };

    // An "incremental" reporter is a simplified model vs. cumulative that presents execution progress in the form of
    // section traversals. In addition to the standarding IStreamingReporter overrides being available, incremental
    // reporters may also use the start and end of section traversals as signals.
    class IncrementalReporterBase : public IStreamingReporter {
        public:
            IncrementalReporterBase( const ReporterConfig& config );
            ~IncrementalReporterBase() override {}

            using SectionTraversalRef = std::reference_wrapper<const IncrementalSectionTraversal>;

            const std::vector<SectionTraversalRef> getTraversals();

        private:
            Catch::TestRunInfo m_currentTestRunInfo;
            Catch::GroupInfo m_currentTestGroupInfo;
            std::vector<Catch::Tag> m_currentTestTags;

            std::vector<IncrementalSectionTraversal> m_completedTraversals;
            IncrementalSectionTraversal m_currentTraversal;

            Detail::unique_ptr<const Catch::IStream> m_incrementalOutputStream;

        protected:
            bool incrementalOutputSupported() const;
            void resetIncrementalOutput();

            std::reference_wrapper<std::ostream> m_outputStreamRef;

            // IStreamingReporter: Default empty implementation provided
            void noMatchingTestCases( std::string const& ) override {};
            void testCaseEnded( TestCaseStats const& ) override {}
            void testGroupEnded( TestGroupStats const& ) override {}
            void assertionStarting( AssertionInfo const& ) override {}
            void testRunEnded( TestRunStats const& ) override {}
            void skipTest( TestCaseInfo const& ) override {}

            // IStreamingReporter: augmented implementations for incremental behavior
            void testRunStarting( TestRunInfo const& ) override;
            void testGroupStarting( GroupInfo const& ) override;
            void testCaseStarting( TestCaseInfo const& ) override;
            void sectionStarting( SectionInfo const& sectionInfo ) override;
            bool assertionEnded( AssertionStats const& assertionStats ) override;
            void sectionEnded( SectionStats const& sectionStats ) override;
            void fatalErrorEncountered( StringRef name ) override;

            // New "hooks" for incremental reporters
            virtual void sectionTraversalStarting( const std::vector<SectionTraversalRef> ) {}
            virtual void sectionTraversalEnded( const std::vector<SectionTraversalRef> ) {}

            // IStreamingReporter:: required boilerplate
            void listReporters( std::vector<ReporterDescription> const& descriptions ) override;
            void listTests( std::vector<TestCaseHandle> const& tests ) override;
            void listTags( std::vector<TagInfo> const& tags ) override;
    };

} // end namespace Catch

#endif // CATCH_REPORTER_CUMULATIVE_BASE_HPP_INCLUDED
