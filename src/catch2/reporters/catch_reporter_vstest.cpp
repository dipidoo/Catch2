/*
 *  Distributed under the Boost Software License, Version 1.0. (See accompanying
 *  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
 */

#include "catch_reporter_vstest.hpp"

#include <catch2/reporters/catch_reporter_helpers.hpp>
#include <catch2/interfaces/catch_interfaces_config.hpp>
#include <catch2/catch_test_spec.hpp>
#include <catch2/catch_tostring.hpp>
#include <catch2/internal/catch_enforce.hpp>
#include <catch2/internal/catch_list.hpp>
#include <catch2/internal/catch_string_manip.hpp>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <random>
#include <sstream>

namespace Catch {

    namespace { // anonymous namespace/this file only

        // Several elements in Vstest require globally unique IDs (GUIDs). Here we use a random
        // generation algorithm that's *not* guaranteed to be truly globally unique, but should
        // be "unique enough" for all reasonable purposes that aren't correlating hundreds of
        // thousands of test runs.
        std::string get_random_not_guaranteed_unique_guid() {
            auto get_random_uint = []() {
                std::random_device random_device;
                std::mt19937 random_generator(random_device());
                std::uniform_int_distribution<unsigned int> random_distribution(
                    std::numeric_limits<unsigned int>::min(),
                    std::numeric_limits<unsigned int>::max());
                return random_distribution(random_generator);
            };

            std::ostringstream guid_stream;

            bool is_first_segment{ true };
            for (const auto segmentLength : { 8, 4, 4, 4, 12 } ) {
                guid_stream << (is_first_segment ? "" : "-");
                is_first_segment = false;

                for (int i = 0; i < segmentLength; i++) {
                    guid_stream << std::hex << (get_random_uint() % 16);
                }
            }

            return guid_stream.str();
        }

        std::string currentTimestamp() {
            return Catch::Detail::stringify( std::chrono::system_clock::now() );
        }

        std::string nanosToDurationString( unsigned long long nanos ) {
            auto totalHns = nanos / 100;
            auto totalSeconds = nanos / 1000000000;
            auto totalMinutes = totalSeconds / 60;
            auto totalHours = totalMinutes / 60;
            ReusableStringStream resultStream;
            resultStream
                << std::setfill('0') << std::setw(2) << totalHours % 60 << ":"
                << std::setfill('0') << std::setw(2) << totalMinutes % 60 << ":"
                << std::setfill('0') << std::setw(2) << totalSeconds % 60 << "."
                << std::setfill('0') << std::setw(-7) << totalHns % 10000000;

            // constexpr auto bufferSize = sizeof( "hh:mm:ss.1234567" );
            // char buffer[bufferSize];
            // std::snprintf( buffer,
            //                bufferSize,
            //                "%02llu:%02llu:%02llu.%07llu",
            //                std::min( totalHours, 99ull ),
            //                totalMinutes % 60,
            //                totalSeconds % 60,
            //                totalHns % 10000000 );
            // return std::string( buffer );
            return resultStream.str();
        }

        // Some consumers of output .trx files (e.g. Azure DevOps Pipelines) fail to ingest results
        // from .trx files if they have certain characters in them. This removes those characters.
        // to-do: make this a parameter or address the root problem of consumers being weird
        std::string getSanitizedTrxName( const std::string& rawName ) {
            ReusableStringStream resultStream;
            auto lastChar = '\0';
            for ( size_t i = 0; i < rawName.length(); ) {
                if ( rawName[i] == '[' ) {
                    if ( rawName.find( ']', i ) == std::string::npos ) {
                        CATCH_ERROR( "Unclosed [tag] in name: " << rawName );
                    }
                    do {
                        i++;
                    } while ( rawName[i - 1] != ']' );
                    if ( lastChar == ' ' && rawName[i] == ' ' ) {
                        // "removed [tag] here" -> "removed  tag" -> "removed
                        // tag"
                        i++;
                    }
                } else if ( rawName[i] == ',' ) {
                    i++;
                } else {
                    lastChar = rawName[i];
                    resultStream << lastChar;
                    i++;
                }
            }
            return trim( resultStream.str() );
        }

        // Evaluates whether the provided context shares the same originating source context as the
        // provided test entry's recorded context data.
        bool contextCouldBeInEntry(
            const StreamingReporterUnwindContext& context,
            const VstestEntry& entry
        ) {
            const auto& firstContextInEntry = entry.unwindContexts[0];
            return context.allSectionInfo[0].name ==
                       firstContextInEntry.allSectionInfo[0].name &&
                   context.allSectionInfo[0].lineInfo.line ==
                       firstContextInEntry.allSectionInfo[0].lineInfo.line;
        }

        // Evaluates whether the provided context shares the same originating source context as the
        // final test entry in a collection.
        bool contextCouldBeInLastEntry(
            const StreamingReporterUnwindContext& context,
            const std::vector<VstestEntry>& entries
        ) {
            if ( entries.empty() )
                return false;

            return !entries.empty() &&
                   contextCouldBeInEntry( context, entries[entries.size() - 1] );
        }

        std::string serializeSourceInfoForStackMessage(
            const Catch::SourceLineInfo& sourceInfo,
            const std::string& sourcePrefix
        ) {
            ReusableStringStream stackStream;
            auto index = 0;
            if ( sourcePrefix.compare( 0,
                                    sourcePrefix.length(),
                                    sourceInfo.file,
                                    sourcePrefix.length() ) == 0 ) {
                index = (int)sourcePrefix.length();
            }
            stackStream << "at Catch.Module.Method() in ";
            for ( auto c = sourceInfo.file[index]; c != '\0';
                c = sourceInfo.file[++index] ) {
                stackStream << ( c == '\\' ? '/' : c );
            }
            stackStream << ":line " << sourceInfo.line << '\n';
            return stackStream.str();
        }
    } // namespace

    StreamingReporterUnwindContext::StreamingReporterUnwindContext():
        hasFatalError{ false } {}

    void StreamingReporterUnwindContext::onFatalErrorCondition(Catch::StringRef) {
        hasFatalError = true;
    }

    void StreamingReporterUnwindContext::addAssertion(
        AssertionStats const& assertionStats ) {
        if ( hasFatalError ) {
            auto info = assertionStats.assertionResult.getSourceInfo();
            fatalAssertionSource = info.file;
            fatalAssertionSource += ":";
            fatalAssertionSource += std::to_string(info.line);
            return;
        }
        allTerminatedAssertions.push_back( assertionStats );
        allExpandedAssertionStatements.push_back(
            assertionStats.assertionResult.getExpandedExpression() );
        for ( auto const& info : assertionStats.infoMessages ) {
            stdOut += "INFO: " + info.message + "\n";
        }
    }

    bool StreamingReporterUnwindContext::unwindIsComplete() const {
        return !allSectionStats.empty() &&
               allSectionStats.size() == allSectionInfo.size();
    }

    void StreamingReporterUnwindContext::clear() {
        allSectionInfo.clear();
        allSectionStats.clear();
        allTerminatedAssertions.clear();
        allExpandedAssertionStatements.clear();
    }

    bool StreamingReporterUnwindContext::hasFailures() const {
        if ( hasFatalError ) {
            return true;
        }
        for ( auto const& assertion : allTerminatedAssertions ) {
            if ( !assertion.assertionResult.isOk() ) {
                return true;
            }
        }
        return false;
    }

    bool StreamingReporterUnwindContext::hasMessages() const {
        return !stdOut.empty() || !stdErr.empty();
    }

    std::string StreamingReporterUnwindContext::constructFullName() const {
        ReusableStringStream nameStream;
        for ( size_t i = 0; i < allSectionInfo.size(); i++ ) {
            nameStream << ( i > 0 ? " / " : "" )
                       << getSanitizedTrxName( allSectionInfo[i].name );
        }
        return nameStream.str();
    }

    std::string StreamingReporterUnwindContext::constructErrorMessage() const {
        ReusableStringStream errorMessageStream;
        if ( !unwindIsComplete() ) {
            errorMessageStream << 
                "Test execution terminated unexpectedly before this test completed. Please see"
                " redirected output for more details." << '\n';
        }

        for ( size_t i = 0; i < allTerminatedAssertions.size(); i++ ) {
            auto&& assertionInfo = allTerminatedAssertions[i];
            auto&& result = assertionInfo.assertionResult;
            if ( result.getResultType() == ResultWas::ExpressionFailed ) {
                // Here we'll write the failure and also its expanded form, if available, e.g.:
                // REQUIRE( x == 1 ) as REQUIRE ( 2 == 1 )
                errorMessageStream << result.getExpressionInMacro();
                if ( allExpandedAssertionStatements[i] !=
                     result.getExpression() ) {
                    errorMessageStream
                        << " as " << result.getTestMacroName() << " ( "
                        << allExpandedAssertionStatements[i] << " ) ";
                }
                errorMessageStream << '\n';
            } else if ( result.getResultType() == ResultWas::ThrewException ) {
                errorMessageStream << "Exception: " << result.getMessage()
                                   << '\n';
            } else if ( !result.isOk() ) {
                errorMessageStream << "Failed: " << result.getMessage() << '\n';
            }
        }

        if ( hasFatalError ) {
            errorMessageStream << "Fatal error at "
                << fatalAssertionSource
                << '\n';
        }

        return errorMessageStream.str();
    }

    // Emits an assertion origination message of the form:
    //  at Catch.Module.Method() in /source/path/file.cpp:line 123
    // Notably:
    //  - A provided prefix (like 'C:\source\project') will be omitted
    //  - Backslashes ('\') will be converted to forward slashes ('/')
    // Incomplete (in progress) unwind contexts will also emit the source info of their latest
    // started section.
    std::string StreamingReporterUnwindContext::constructStackMessage(
        std::string const& sourcePrefix
    ) const {
        ReusableStringStream stackStream;
        for ( auto const& assertionInfo : allTerminatedAssertions ) {
            auto&& sourceInfo = assertionInfo.assertionResult.getSourceInfo();
            stackStream << serializeSourceInfoForStackMessage(sourceInfo, sourcePrefix);
        }
        if (!unwindIsComplete()) {
            auto&& lastSection = allSectionInfo[allSectionInfo.size() - 1];
            stackStream << serializeSourceInfoForStackMessage(lastSection.lineInfo, sourcePrefix);
        }
        return stackStream.str();
    }

    std::string StreamingReporterUnwindContext::constructDuration() const {
        return nanosToDurationString( elapsedNanoseconds );
    }

    VstestEntry::VstestEntry( std::string name ):
        name( getSanitizedTrxName( name ) ),
        testId( get_random_not_guaranteed_unique_guid() ),
        executionId( get_random_not_guaranteed_unique_guid() ) {}

    bool VstestEntry::hasFailures() const {
        for ( auto&& unwindContext : unwindContexts ) {
            if ( unwindContext.hasFailures() ) {
                return true;
            }
        }
        return false;
    }

    std::string VstestEntry::constructDuration() const {
        auto total = 0ull;
        for ( auto&& context : unwindContexts ) {
            total += context.elapsedNanoseconds;
        }
        return nanosToDurationString( total );
    }

    VstestReporter::VstestReporter( ReporterConfig const& _config ):
        StreamingReporterBase{ _config },
        m_xml{ nullptr },
        m_emissionType{ TrxEmissionType::Intermediate },
        m_defaultTestListId{ get_random_not_guaranteed_unique_guid() },
        m_doIncrementalXmlOutput{ false } {

        m_preferences.shouldRedirectStdOut = true;
        m_preferences.shouldReportAllAssertions = true;
        m_config = _config.fullConfig();
        m_doIncrementalXmlOutput = m_config->standardOutputRedirect() ||
                                   m_config->standardErrorRedirect();
    }

    //
    // StreamingReporterBase implementation
    //

    void VstestReporter::noMatchingTestCases( std::string const& s ) {
        StreamingReporterBase::noMatchingTestCases( s );
    }

    void VstestReporter::fatalErrorEncountered( Catch::StringRef signalName ) {
        m_currentUnwindContext.onFatalErrorCondition( signalName );
    }

    void VstestReporter::testRunStarting( TestRunInfo const& testInfo ) {
        StreamingReporterBase::testRunStarting( testInfo );
    }

    void VstestReporter::testGroupStarting( GroupInfo const& groupInfo ) {
        m_runName = groupInfo.name;
        StreamingReporterBase::testGroupStarting( groupInfo );
    }

    void VstestReporter::testCaseStarting( TestCaseInfo const& testInfo ) {
        m_currentTestCaseTags = testInfo.tags;
        StreamingReporterBase::testCaseStarting( testInfo );
    }

    void VstestReporter::sectionStarting( SectionInfo const& sectionInfo ) {
        StreamingReporterBase::sectionStarting( sectionInfo );
        if ( m_currentUnwindContext.allSectionInfo.empty() ) {
            m_timer.start();
            m_currentUnwindContext.startTimestamp = currentTimestamp();
        }
        m_currentUnwindContext.allSectionInfo.push_back( sectionInfo );

        if ( m_doIncrementalXmlOutput ) {
            emitTrx();        
        }
    }

    void VstestReporter::assertionStarting( AssertionInfo const& ) {}

    bool VstestReporter::assertionEnded( AssertionStats const& assertionStats ) {
        if (!assertionStats.assertionResult.isOk()) {
          m_currentUnwindContext.addAssertion( assertionStats );
        }
        return true;
    }

    void VstestReporter::sectionEnded( SectionStats const& sectionStats ) {
        m_currentUnwindContext.allSectionStats.push_back( sectionStats );

        if ( m_currentUnwindContext.unwindIsComplete() ) {
            flushCurrentUnwindContext();
        }

        StreamingReporterBase::sectionEnded( sectionStats );
    }

    void VstestReporter::testCaseEnded( TestCaseStats const& testCaseStats ) {
        StreamingReporterBase::testCaseEnded( testCaseStats );
    }

    void VstestReporter::testGroupEnded( TestGroupStats const& testGroupStats ) {
        StreamingReporterBase::testGroupEnded( testGroupStats );
    }

    void VstestReporter::testRunEnded( TestRunStats const& testRunStats ) {
        m_emissionType = TrxEmissionType::Final;
        emitTrx();
        StreamingReporterBase::testRunEnded( testRunStats );
    }

    //
    // .trx emission -- see vstst.xsd for schema details
    //

    void VstestReporter::startTestRunElement() {
        m_xml->startElement( "TestRun" );
        m_xml->writeAttribute( "id", get_random_not_guaranteed_unique_guid() );
        m_xml->writeAttribute( "name", m_runName );
        m_xml->writeAttribute( "runUser", "Catch2VstestReporter" );
        m_xml->writeAttribute(
            "xmlns",
            "http://microsoft.com/schemas/VisualStudio/TeamTest/2010" );
    }

    void VstestReporter::writeTimesElement() {
        auto now = currentTimestamp();
        auto startTime = now;
        auto endTime = now;

        if ( !m_completedTestEntries.empty() ) {
            auto&& firstUnwind = m_completedTestEntries[0].unwindContexts[0];
            auto&& lastEntry = m_completedTestEntries[m_completedTestEntries.size() - 1];
            auto&& lastUnwind =
                lastEntry.unwindContexts[lastEntry.unwindContexts.size() - 1];
            startTime = firstUnwind.startTimestamp;
            endTime = lastUnwind.endTimestamp;
        }

        m_xml->scopedElement( "Times" )
            .writeAttribute( "creation", startTime )
            .writeAttribute( "queuing", startTime )
            .writeAttribute( "start", startTime )
            .writeAttribute( "finish", endTime );
    }

    void VstestReporter::writeUnwindOutput(
        StreamingReporterUnwindContext const& unwindContext ) {

        const auto& inProgress =
            m_emissionType == TrxEmissionType::Intermediate &&
            !unwindContext.unwindIsComplete();

        if ( unwindContext.hasMessages() || unwindContext.hasFailures() || inProgress) {
            auto outputElement = m_xml->scopedElement( "Output" );
            if ( !unwindContext.stdOut.empty() || inProgress ) {
                m_xml->scopedElement( "StdOut" )
                    .writeText( unwindContext.stdOut, XmlFormatting::Newline );
            }
            if ( !unwindContext.stdErr.empty() || inProgress ) {
                m_xml->scopedElement( "StdErr" )
                    .writeText( unwindContext.stdErr, XmlFormatting::Newline );
            }
            auto errorMessage = unwindContext.constructErrorMessage();
            auto stackMessage =
                unwindContext.constructStackMessage( m_config->sourcePathPrefix() );
            if ( !errorMessage.empty() || !stackMessage.empty() ) {
                auto errorInfoElement = m_xml->scopedElement( "ErrorInfo" );
                if ( !errorMessage.empty() ) {
                    m_xml->scopedElement( "Message" ).writeText( errorMessage, XmlFormatting::Newline );
                }
                if ( !stackMessage.empty() ) {
                    m_xml->scopedElement( "StackTrace" )
                        .writeText( stackMessage, XmlFormatting::Newline );
                }
            }
        }
    }

    void
    VstestReporter::startUnitTestResultElement( const std::string& executionId,
                                                const std::string& testId,
                                                const std::string& name ) {
        constexpr auto g_vsTestType = "13cdc9d9-ddb5-4fa4-a97d-d965ccfc6d4b";
        constexpr auto g_computerName = "localhost";

        m_xml->startElement( "UnitTestResult" );
        m_xml->writeAttribute( "executionId", executionId );
        m_xml->writeAttribute( "testId", testId );
        m_xml->writeAttribute( "testName", name );
        m_xml->writeAttribute( "computerName", g_computerName );
        m_xml->writeAttribute( "testType", g_vsTestType );
        m_xml->writeAttribute( "testListId", m_defaultTestListId );
    }

    void VstestReporter::writeInnerResult(
        StreamingReporterUnwindContext const& unwindContext,
        const std::string& parentExecutionId ) {

        auto isInProgress = (m_emissionType == TrxEmissionType::Intermediate)
            && !unwindContext.unwindIsComplete();

        auto name = unwindContext.constructFullName();
        name = name.empty() ? "Unknown test" : name;
        name = isInProgress ? name + " (in progress)" : name;

        startUnitTestResultElement(
            get_random_not_guaranteed_unique_guid(),
            get_random_not_guaranteed_unique_guid(),
            name );
        m_xml->writeAttribute( "parentExecutionId", parentExecutionId );
        m_xml->writeAttribute( "resultType", "DataDrivenDataRow" );
        m_xml->writeAttribute( "startTime", unwindContext.startTimestamp );
        m_xml->writeAttribute( "endTime", unwindContext.endTimestamp );
        m_xml->writeAttribute( "duration", unwindContext.constructDuration() );

        m_xml->writeAttribute(
            "outcome",
            unwindContext.hasFailures() || isInProgress ? "Failed" : "Passed" );
        writeUnwindOutput( unwindContext );
        m_xml->endElement(); // UnitTestResult
    }

    void VstestReporter::writeToplevelResult( VstestEntry const& testEntry ) {
        startUnitTestResultElement(
            testEntry.executionId, testEntry.testId, testEntry.name );

        auto&& firstUnwind = testEntry.unwindContexts[0];
        auto&& lastUnwind =
            testEntry.unwindContexts[testEntry.unwindContexts.size() - 1];
        m_xml->writeAttribute( "startTime", firstUnwind.startTimestamp );
        m_xml->writeAttribute( "endTime", lastUnwind.endTimestamp );
        m_xml->writeAttribute( "duration", testEntry.constructDuration() );
        m_xml->writeAttribute( "outcome",
                              testEntry.hasFailures() ? "Failed" : "Passed" );

        if ( testEntry.unwindContexts.size() == 1 
            && m_emissionType == TrxEmissionType::Final ) {
            // This is a flat test (no sections/sub-results) and gets its
            // details in the top element from its single unwind
            writeUnwindOutput( testEntry.unwindContexts[0] );
        } else {
            m_xml->writeAttribute( "resultType", "DataDrivenTest" );

            auto innerResultsElement = m_xml->scopedElement( "InnerResults" );
            for ( auto const& unwindContext : testEntry.unwindContexts ) {
                if ( unwindContext.unwindIsComplete() ) {
                    writeInnerResult( unwindContext, testEntry.executionId );                
                }
            }

            if ( m_emissionType == TrxEmissionType::Intermediate &&
                 contextCouldBeInEntry( m_currentUnwindContext, testEntry ) ) {
                writeInnerResult( m_currentUnwindContext,
                                  testEntry.executionId );
            }
        }

        m_xml->endElement(); // UnitTestResult
    }

    void VstestReporter::writeResults() {
        auto resultsElement = m_xml->scopedElement( "Results" );
        for ( auto const& testEntry : m_completedTestEntries ) {
            writeToplevelResult( testEntry );
        }

        // If the first unwind context of a test is in progress, emit a top-level result for it.
        // Use the chain of section names to determine if it's instead another unwind in the
        // current test.
        if (m_emissionType == TrxEmissionType::Intermediate
            && !m_currentUnwindContext.unwindIsComplete()
            && !contextCouldBeInLastEntry(m_currentUnwindContext, m_completedTestEntries)) {

            auto tempEntry{
                VstestEntry{ m_currentUnwindContext.allSectionInfo[0].name } };
            tempEntry.unwindContexts.push_back( m_currentUnwindContext );
            writeToplevelResult( tempEntry );
        }
    }

    void VstestReporter::writeTestDefinitions() {
        auto testDefinitionsElement = m_xml->scopedElement( "TestDefinitions" );
        for ( auto const& testEntry : m_completedTestEntries ) {
            auto unitTestElement = m_xml->scopedElement( "UnitTest" );
            m_xml->writeAttribute( "name", testEntry.name );
            m_xml->writeAttribute( "storage", m_runName );
            m_xml->writeAttribute( "id", testEntry.testId );
            if ( !testEntry.tags.empty() ) {
                auto testCategoriesElement =
                    m_xml->scopedElement( "TestCategory" );
                for ( auto const& tag : testEntry.tags ) {
                    m_xml->scopedElement( "TestCategoryItem" )
                        .writeAttribute( "TestCategory", tag.original );
                }
            }
            m_xml->scopedElement( "Execution" )
                .writeAttribute( "id", testEntry.executionId );
            m_xml->scopedElement( "TestMethod" )
                .writeAttribute( "codeBase", m_runName )
                .writeAttribute( "adapterTypeName",
                                 "executor://mstestadapter/v2" )
                .writeAttribute( "className", "Catch2.Test" )
                .writeAttribute( "name", testEntry.name );
        }
    }

    void VstestReporter::writeTestEntries() {
        auto testEntriesElement = m_xml->scopedElement( "TestEntries" );
        for ( auto const& testEntry : m_completedTestEntries ) {
            m_xml->scopedElement( "TestEntry" )
                .writeAttribute( "testId", testEntry.testId )
                .writeAttribute( "executionId", testEntry.executionId )
                .writeAttribute( "testListId", m_defaultTestListId );
        }
    }

    void VstestReporter::writeTestLists() {
        auto testListsElement = m_xml->scopedElement( "TestLists" );
        m_xml->scopedElement( "TestList" )
            .writeAttribute( "name", "Default test list for Catch2" )
            .writeAttribute( "id", m_defaultTestListId );
    }

    void VstestReporter::writeSummaryElement() {
        auto resultSummaryElement = m_xml->scopedElement( "ResultSummary" );

        auto hasFailures = false;
        for ( auto const& testEntry : m_completedTestEntries ) {
            if ( testEntry.hasFailures() ) {
                hasFailures = true;
                break;
            }
        }

        hasFailures |= m_emissionType == TrxEmissionType::Intermediate;

        resultSummaryElement.writeAttribute(
            "outcome", hasFailures ? "Failed" : "Passed" );

        const auto& attachments = m_config->reportAttachmentPaths();
        if ( !attachments.empty() ) {
            auto resultFilesElement = m_xml->scopedElement( "ResultFiles" );
            for ( const auto& attachmentPath : attachments ) {
                m_xml->scopedElement( "ResultFile" ).writeAttribute( "path", attachmentPath );
            }
        }
    }

    void VstestReporter::emitTrx() {
        if ( m_xml ) {
            m_xml = nullptr;
            const_cast<Catch::IConfig*>( m_config )->resetOutputStream();
        }
        m_xml = Detail::make_unique<XmlWriter>( m_config->stream() );

        startTestRunElement();
        writeTimesElement();
        writeResults();
        writeTestDefinitions();
        writeTestEntries();
        writeTestLists();
        writeSummaryElement();
        m_xml->endElement(); // TestRun
    }

    void VstestReporter::flushCurrentUnwindContext() {
        if ( m_currentUnwindContext.allSectionStats.empty() ) {
            return;
        }

#ifdef CATCH_CONFIG_NEW_CAPTURE
        // If we're using output redirect sinks, this is the cue that everything we currently have
        // in the redirect sink should be associated with the test we're wrapping up. Record it and
        // reset the sinks for future tests.
        if (m_config->standardOutputRedirect()) {
            m_currentUnwindContext.stdOut += m_config->standardOutputRedirect()->getContents();
            m_config->standardOutputRedirect()->reset();
        }
        if (m_config->standardErrorRedirect()) {
            m_currentUnwindContext.stdErr += m_config->standardErrorRedirect()->getContents();
            m_config->standardErrorRedirect()->reset();
        }
#endif

        auto latestTest = [&]() {
            return &m_completedTestEntries[m_completedTestEntries.size() - 1];
        };

        m_currentUnwindContext.elapsedNanoseconds = m_timer.getElapsedNanoseconds();
        auto&& currentStats = m_currentUnwindContext.allSectionStats;
        auto&& currentToplevelName = currentStats[currentStats.size() - 1].sectionInfo.name;
        bool isUnwindFromCurrentTest = !m_completedTestEntries.empty() &&
            latestTest()->name == getSanitizedTrxName( currentToplevelName );

        if ( !isUnwindFromCurrentTest ) {
            VstestEntry newEntry{ currentToplevelName };
            newEntry.tags = m_currentTestCaseTags;
            m_completedTestEntries.push_back( std::move( newEntry ) );
        }

        m_currentUnwindContext.endTimestamp = currentTimestamp();
        latestTest()->unwindContexts.push_back( std::move( m_currentUnwindContext ) );

        m_currentUnwindContext.clear();
    }
} // end namespace Catch