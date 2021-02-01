/*
 *  Distributed under the Boost Software License, Version 1.0. (See accompanying
 *  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
 */
#ifndef CATCH_REPORTER_VSTEST_HPP_INCLUDED
#define CATCH_REPORTER_VSTEST_HPP_INCLUDED

#include <catch2/catch_test_case_info.hpp>
#include <catch2/catch_timer.hpp>
#include <catch2/internal/catch_xmlwriter.hpp>
#include <catch2/reporters/catch_reporter_streaming_base.hpp>

#ifdef __clang__
#   pragma clang diagnostic push
#   pragma clang diagnostic ignored "-Wpadded"
#endif

namespace Catch {

    // An "unwind context" represents a single depth-first traversal of a section hierarchy used for
    // text execution. E.g. a single test case could have a structure such as:
    //
    // <TestCase name="explanatory test case">
    //   <Section name="first top-level section">
    //     <Section name="first subsection"/>
    //     <Section name="second subsection"/>
    //   </Section>
    //   <Section name="second top-level section"/>
    // </TestCase>
    //
    // This test case has three unique root-to-leaf traversals and thus three "unwinds":
    //  1. explanatory test case / first top-level section / first subsection
    //  2. explanatory test case / first top-level section / second subsection
    //  3. explanatory test case / second top-level section
    class StreamingReporterUnwindContext {
    public:
        StreamingReporterUnwindContext();
        std::vector<SectionInfo> allSectionInfo;
        std::vector<SectionStats> allSectionStats;
        std::vector<AssertionStats> allTerminatedAssertions;
        std::vector<std::string> allExpandedAssertionStatements;
        std::string startTimestamp;
        std::string endTimestamp;
        std::string stdOut;
        std::string stdErr;
        bool hasFatalError;
        std::string fatalAssertionSource;
        unsigned long long elapsedNanoseconds;

    public:
        void addAssertion( AssertionStats const& assertionStats );
        void onFatalErrorCondition( Catch::StringRef signalName );
        bool unwindIsComplete() const;
        void clear();
        bool hasFailures() const;
        bool hasMessages() const;
        std::string constructFullName() const;
        std::string constructErrorMessage() const;
        std::string constructStackMessage(std::string const& sourcePrefix) const;
        std::string constructDuration() const;
    };

    // VstestEntry is a container representing the collection of unwind contexts and associated
    // metadata associated with a single Testcase.
    class VstestEntry {
    public:
        VstestEntry( std::string name );

    public:
        std::string name;
        std::vector<Catch::Tag> tags;
        std::string testId;
        std::string executionId;
        std::string startTimestamp;
        std::string endTimestamp;
        std::vector<StreamingReporterUnwindContext> unwindContexts;

    public:
        bool hasFailures() const;
        std::string constructDuration() const;
    };

    class VstestReporter : public StreamingReporterBase {
    private:
        enum class TrxEmissionType {
            // This is an intermediate emission (during tests) that's assumed to be catastrophic
            // failure if not later replaced by final emission
            Intermediate,
            // This is the final emission (after all tests) with all results present
            Final,
        };

    private:
        Detail::unique_ptr<XmlWriter> m_xml;
        TrxEmissionType m_emissionType;
        Timer m_timer;
        std::string m_runName;
        std::string m_defaultTestListId;
        std::vector<Catch::Tag> m_currentTestCaseTags;
        std::vector<VstestEntry> m_completedTestEntries;
        StreamingReporterUnwindContext m_currentUnwindContext;
        bool m_handlingFatalSignal;

    public:
        VstestReporter( ReporterConfig const& _config );
        ~VstestReporter() override {}

        static std::string getDescription() {
            return "Reports test in .trx XML format, conformant to Vstest v2";
        }

    private: // trx emission methods
        void startTestRunElement();
        void writeTimesElement();
        void writeResults();
        void startUnitTestResultElement(
            const std::string& executionId,
            const std::string& testId,
            const std::string& name );
        void writeToplevelResult( VstestEntry const& testEntry );
        void writeInnerResult(
            StreamingReporterUnwindContext const& unwindContext,
            const std::string& parentExecutionId );
        void writeUnwindOutput(StreamingReporterUnwindContext const& unwindContext );
        void writeTestDefinitions();
        void writeTestEntries();
        void writeTestLists();
        void writeSummaryElement();

        void emitTrx();

    private:
        void flushCurrentUnwindContext();

    public: // StreamingReporterBase implementation
        void noMatchingTestCases( std::string const& s ) override;
        void fatalErrorEncountered( Catch::StringRef signalName ) override;
        void testRunStarting( TestRunInfo const& testInfo ) override;
        void testGroupStarting( GroupInfo const& groupInfo ) override;
        void testCaseStarting( TestCaseInfo const& testInfo ) override;
        void sectionStarting( SectionInfo const& sectionInfo ) override;
        void assertionStarting( AssertionInfo const& ) override;
        bool assertionEnded( AssertionStats const& assertionStats ) override;
        void sectionEnded( SectionStats const& sectionStats ) override;
        void testCaseEnded( TestCaseStats const& testCaseStats ) override;
        void testGroupEnded( TestGroupStats const& testGroupStats ) override;
        void testRunEnded( TestRunStats const& testRunStats ) override;
    };

} // end namespace Catch

#ifdef __clang__
#   pragma clang diagnostic pop
#endif

#endif // CATCH_REPORTER_VSTEST_HPP_INCLUDED