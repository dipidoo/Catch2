/*
 *  Distributed under the Boost Software License, Version 1.0. (See accompanying
 *  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
 */
#ifndef TWOBLUECUBES_CATCH_REPORTER_VSTEST_H_INCLUDED
#define TWOBLUECUBES_CATCH_REPORTER_VSTEST_H_INCLUDED


#include "catch_reporter_bases.hpp"
#include "../internal/catch_xmlwriter.h"
#include "../internal/catch_timer.h"

namespace Catch {

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
        bool hasPendingErrors() const;
        std::string constructFullName() const;
        std::string constructErrorMessage() const;
        std::string constructStackMessage(std::string const& sourcePrefix) const;
        std::string constructDuration() const;
    };

        class VstestEntry {
    public:
            VstestEntry( std::string name );
            
    public:
        std::string name;
        std::vector<std::string> tags;
        std::string testId;
        std::string executionId;
        std::string startTimestamp;
        std::string endTimestamp;
        std::vector<StreamingReporterUnwindContext> unwindContexts;

    public:
        bool hasFailures() const;
        std::string constructDuration() const;
    };

    class VstestReporter : public StreamingReporterBase<VstestReporter> {
    private:
        XmlWriter m_xml;
        Timer m_timer;
        IConfigPtr m_config;
        std::string m_runName;
        std::string m_defaultTestListId;
        std::vector<std::string> m_currentTestCaseTags;
        std::vector<VstestEntry> m_testEntries;
        StreamingReporterUnwindContext m_currentUnwindContext;
        bool m_handlingFatalSignal;

    public:
        VstestReporter( ReporterConfig const& _config );

        ~VstestReporter() override {}

        static std::string getDescription() {
            return "Reports test in .trx format like VsTest v2";
        }

    private: // trx emission methods
        void startTestRunElement();

        void writeTimesElement();
        void writeUnwindOutput(
            StreamingReporterUnwindContext const& unwindContext );
        void startUnitTestResultElement( const std::string& executionId,
                                         const std::string& testId,
                                         const std::string& name );
        void
        writeInnerResult( StreamingReporterUnwindContext const& unwindContext,
                          const std::string& parentExecutionId );
        void writeToplevelResult( VstestEntry const& testEntry );

        void writeResults();

        void writeTestDefinitions();

        void writeTestEntries();

        void writeTestLists();

        void writeSummaryElement();

        void emitTrx();

    private:
        void flushCurrentUnwindContext(
            const SectionStats* lastSectionStats = nullptr );

    public: // StreamingReporterBase
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

#endif // TWOBLUECUBES_CATCH_REPORTER_VSTEST_H_INCLUDED
