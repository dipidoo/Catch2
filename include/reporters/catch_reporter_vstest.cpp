/*
 *  Distributed under the Boost Software License, Version 1.0. (See accompanying
 *  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
 */

#include "catch_reporter_vstest.h"

#include "../internal/catch_reporter_registrars.hpp"
#include "../internal/catch_text.h"
#include "../internal/catch_tostring.h"
#include "catch_reporter_bases.hpp"

#ifdef CATCH_PLATFORM_WINDOWS
#define NOMINMAX
#include <Rpc.h>
#else
#include <uuid/uuid.h>
#endif

#include <algorithm>
#include <cassert>
#include <ctime>
#include <sstream>

namespace Catch {

    namespace {
        std::string createGuid() {
#ifdef CATCH_PLATFORM_WINDOWS
            UUID uuid;
            UuidCreate( &uuid );

            unsigned char* str;
            UuidToStringA( &uuid, &str );

            std::string s( (char*)str );

            RpcStringFreeA( &str );
#else
            uuid_t uuid;
            uuid_generate_random( uuid );
            char s[37];
            uuid_unparse( uuid, s );
#endif
            return s;
        }

        std::string currentTimestamp() {
            // Beware, this is not reentrant because of backward compatibility
            // issues Also, UTC only, again because of backward compatibility
            // (%z is C++11)
            time_t rawtime;
            std::time( &rawtime );
            auto const timeStampSize = sizeof( "2017-01-16T17:06:45Z" );

#ifdef _MSC_VER
            std::tm timeInfo = {};
            gmtime_s( &timeInfo, &rawtime );

#else
            std::tm* timeInfo;
            timeInfo = std::gmtime( &rawtime );
#endif

            char timeStamp[timeStampSize];
            const char* const fmt = "%Y-%m-%dT%H:%M:%SZ";

#ifdef _MSC_VER
            std::strftime( timeStamp, timeStampSize, fmt, &timeInfo );
#else
            std::strftime( timeStamp, timeStampSize, fmt, timeInfo );
#endif
            return std::string( timeStamp );
        }

        std::string nanosToDurationString( unsigned long long nanos ) {
            auto totalSeconds = nanos / 1000000000;
            auto totalMinutes = totalSeconds / 60;
            auto totalHours = totalMinutes / 60;
            constexpr auto bufferSize = sizeof( "hh:mm:ss.1234567" );
            char buffer[bufferSize];
            std::snprintf( buffer,
                           bufferSize,
                           "%02llu:%02llu:%02llu.%07llu",
                           std::min( totalHours, 99ull ),
                           totalMinutes % 60,
                           totalSeconds % 60,
                           ( nanos / 100 ) % 10000000 );
            return std::string( buffer );
        }

        // Some consumers of output .trx files (e.g. Azure DevOps Pipelines)
        // fail to ingest results from .trx files if they have certain
        // characters in them. This removes those characters. to-do: make this a
        // parameter or address the root problem of consumers being weird
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
    } // namespace

    StreamingReporterUnwindContext::StreamingReporterUnwindContext(): hasFatalError(false) {}

    void StreamingReporterUnwindContext::onFatalErrorCondition(
        Catch::StringRef) {
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
        hasFatalError = false;
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
    bool StreamingReporterUnwindContext::hasPendingErrors() const {
        return hasFatalError || !allTerminatedAssertions.empty();
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
        for ( size_t i = 0; i < allTerminatedAssertions.size(); i++ ) {
            auto&& assertionInfo = allTerminatedAssertions[i];
            auto&& result = assertionInfo.assertionResult;
            if ( result.getResultType() == ResultWas::ExpressionFailed ) {
                // Here we'll write the failure and also its expanded form, if
                // available: REQUIRE( x == 1 ) REQUIRE( x == 1 ) as REQUIRE ( 2
                // == 1 )
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
    std::string StreamingReporterUnwindContext::constructStackMessage(
        std::string const& sourcePrefix) const {
        ReusableStringStream stackStream;
        for ( auto const& assertionInfo : allTerminatedAssertions ) {
            auto&& sourceInfo = assertionInfo.assertionResult.getSourceInfo();
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
        }
        return stackStream.str();
    }
    std::string StreamingReporterUnwindContext::constructDuration() const {
        return nanosToDurationString( elapsedNanoseconds );
    }

    VstestEntry::VstestEntry( std::string name ):
        name( getSanitizedTrxName( name ) ),
        testId( createGuid() ),
        executionId( createGuid() ) {}

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
        StreamingReporterBase( _config ),
        m_xml( _config.stream() ),
        m_defaultTestListId{ createGuid() } {
        m_config = _config.fullConfig();
        m_reporterPrefs.shouldRedirectStdOut = true;
        fopen_s( &(m_reporterPrefs.stdoutRedirect), 
            (m_config->getOutputFilename() + ".out").c_str(),
            "w+" );
        fopen_s( &(m_reporterPrefs.stderrRedirect),
            (m_config->getOutputFilename() + ".err").c_str(),
            "w+" );
        m_reporterPrefs.shouldReportAllAssertions = true;
    }

    // StreamingReporterBase implementation

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
    }

    void VstestReporter::assertionStarting( AssertionInfo const& ) {}

    bool
    VstestReporter::assertionEnded( AssertionStats const& assertionStats ) {
        m_currentUnwindContext.addAssertion( assertionStats );
        return true;
    }

    void VstestReporter::sectionEnded( SectionStats const& sectionStats ) {
        m_currentUnwindContext.allSectionStats.push_back( sectionStats );

        if ( m_currentUnwindContext.unwindIsComplete() ) {
            flushCurrentUnwindContext( &sectionStats );
        }

        StreamingReporterBase::sectionEnded( sectionStats );
    }

    void VstestReporter::testCaseEnded( TestCaseStats const& testCaseStats ) {
        StreamingReporterBase::testCaseEnded( testCaseStats );
    }

    void
    VstestReporter::testGroupEnded( TestGroupStats const& testGroupStats ) {
        StreamingReporterBase::testGroupEnded( testGroupStats );
    }

    void VstestReporter::testRunEnded( TestRunStats const& testRunStats ) {
        if ( m_currentUnwindContext.hasPendingErrors() ) {
            // The test run ended unexpectedly (without matched section
            // starts and section ends), which usually happens because of a
            // segfault or similar in a test. Flush the last entry so we get
            // the info.
            m_currentUnwindContext.stdErr +=
                "<Test aborted unexpectedly; output may be incomplete>\n";
            flushCurrentUnwindContext();
        }
        emitTrx();
        StreamingReporterBase::testRunEnded( testRunStats );
    }

    // .trx emission
    void VstestReporter::startTestRunElement() {
        m_xml.startElement( "TestRun" );
        m_xml.writeAttribute( "id", createGuid() );
        m_xml.writeAttribute( "name", m_runName );
        m_xml.writeAttribute( "runUser", "Catch2VstestReporter" );
        m_xml.writeAttribute(
            "xmlns",
            "http://microsoft.com/schemas/VisualStudio/TeamTest/2010" );
    }

    void VstestReporter::writeTimesElement() {
        auto now = currentTimestamp();
        auto startTime = now;
        auto endTime = now;

        if ( !m_testEntries.empty() ) {
            auto&& firstUnwind = m_testEntries[0].unwindContexts[0];
            auto&& lastEntry = m_testEntries[m_testEntries.size() - 1];
            auto&& lastUnwind =
                lastEntry.unwindContexts[lastEntry.unwindContexts.size() - 1];
            startTime = firstUnwind.startTimestamp;
            endTime = lastUnwind.endTimestamp;
        }

        m_xml.scopedElement( "Times" )
            .writeAttribute( "creation", startTime )
            .writeAttribute( "queuing", startTime )
            .writeAttribute( "start", startTime )
            .writeAttribute( "finish", endTime );
    }

    void VstestReporter::writeUnwindOutput(
        StreamingReporterUnwindContext const& unwindContext ) {
        if ( unwindContext.hasMessages() || unwindContext.hasFailures() ) {
            auto outputElement = m_xml.scopedElement( "Output" );
            if ( !unwindContext.stdOut.empty() ) {
                m_xml.scopedElement( "StdOut" )
                    .writeText( unwindContext.stdOut, XmlFormatting::Newline );
            }
            if ( !unwindContext.stdErr.empty() ) {
                m_xml.scopedElement( "StdErr" )
                    .writeText( unwindContext.stdErr, XmlFormatting::Newline );
            }
            auto errorMessage = unwindContext.constructErrorMessage();
            auto stackMessage =
                unwindContext.constructStackMessage( m_config->sourcePrefix() );
            if ( !errorMessage.empty() || !stackMessage.empty() ) {
                auto errorInfoElement = m_xml.scopedElement( "ErrorInfo" );
                if ( !errorMessage.empty() ) {
                    m_xml.scopedElement( "Message" ).writeText( errorMessage, XmlFormatting::Newline );
                }
                if ( !stackMessage.empty() ) {
                    m_xml.scopedElement( "StackTrace" )
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

        m_xml.startElement( "UnitTestResult" );
        m_xml.writeAttribute( "executionId", executionId );
        m_xml.writeAttribute( "testId", testId );
        m_xml.writeAttribute( "testName", name );
        m_xml.writeAttribute( "computerName", g_computerName );
        m_xml.writeAttribute( "testType", g_vsTestType );
        m_xml.writeAttribute( "testListId", m_defaultTestListId );
    }

    void VstestReporter::writeInnerResult(
        StreamingReporterUnwindContext const& unwindContext,
        const std::string& parentExecutionId ) {
        auto name = unwindContext.constructFullName();
        name = name.empty() ? "Unknown test" : name;

        startUnitTestResultElement( createGuid(), createGuid(), name );
        m_xml.writeAttribute( "parentExecutionId", parentExecutionId );
        m_xml.writeAttribute( "resultType", "DataDrivenDataRow" );
        m_xml.writeAttribute( "startTime", unwindContext.startTimestamp );
        m_xml.writeAttribute( "endTime", unwindContext.endTimestamp );
        m_xml.writeAttribute( "duration", unwindContext.constructDuration() );
        m_xml.writeAttribute(
            "outcome", unwindContext.hasFailures() ? "Failed" : "Passed" );
        writeUnwindOutput( unwindContext );
        m_xml.endElement(); // UnitTestResult
    }

    void VstestReporter::writeToplevelResult( VstestEntry const& testEntry ) {
        startUnitTestResultElement(
            testEntry.executionId, testEntry.testId, testEntry.name );

        auto&& firstUnwind = testEntry.unwindContexts[0];
        auto&& lastUnwind =
            testEntry.unwindContexts[testEntry.unwindContexts.size() - 1];
        m_xml.writeAttribute( "startTime", firstUnwind.startTimestamp );
        m_xml.writeAttribute( "endTime", lastUnwind.endTimestamp );
        m_xml.writeAttribute( "duration", testEntry.constructDuration() );
        m_xml.writeAttribute( "outcome",
                              testEntry.hasFailures() ? "Failed" : "Passed" );

        if ( testEntry.unwindContexts.size() == 1 ) {
            // This is a flat test (no sections/sub-results) and gets its
            // details in the top element from its single unwind
            writeUnwindOutput( testEntry.unwindContexts[0] );
        } else {
            m_xml.writeAttribute( "resultType", "DataDrivenTest" );

            auto innerResultsElement = m_xml.scopedElement( "InnerResults" );
            for ( auto const& unwindContext : testEntry.unwindContexts ) {
                writeInnerResult( unwindContext, testEntry.executionId );
            }
        }

        m_xml.endElement(); // UnitTestResult
    }

    void VstestReporter::writeResults() {
        auto resultsElement = m_xml.scopedElement( "Results" );
        for ( auto const& testEntry : m_testEntries ) {
            writeToplevelResult( testEntry );
        }
    }

    void VstestReporter::writeTestDefinitions() {
        auto testDefinitionsElement = m_xml.scopedElement( "TestDefinitions" );
        for ( auto const& testEntry : m_testEntries ) {
            auto unitTestElement = m_xml.scopedElement( "UnitTest" );
            m_xml.writeAttribute( "name", testEntry.name );
            m_xml.writeAttribute( "storage", m_runName );
            m_xml.writeAttribute( "id", testEntry.testId );
            if ( !testEntry.tags.empty() ) {
                auto testCategoriesElement =
                    m_xml.scopedElement( "TestCategory" );
                for ( auto const& tag : testEntry.tags ) {
                    m_xml.scopedElement( "TestCategoryItem" )
                        .writeAttribute( "TestCategory", tag );
                }
            }
            m_xml.scopedElement( "Execution" )
                .writeAttribute( "id", testEntry.executionId );
            m_xml.scopedElement( "TestMethod" )
                .writeAttribute( "codeBase", m_runName )
                .writeAttribute( "adapterTypeName",
                                 "executor://mstestadapter/v2" )
                .writeAttribute( "className", "Catch2.Test" )
                .writeAttribute( "name", testEntry.name );
        }
    }

    void VstestReporter::writeTestEntries() {
        auto testEntriesElement = m_xml.scopedElement( "TestEntries" );
        for ( auto const& testEntry : m_testEntries ) {
            m_xml.scopedElement( "TestEntry" )
                .writeAttribute( "testId", testEntry.testId )
                .writeAttribute( "executionId", testEntry.executionId )
                .writeAttribute( "testListId", m_defaultTestListId );
        }
    }

    void VstestReporter::writeTestLists() {
        auto testListsElement = m_xml.scopedElement( "TestLists" );
        m_xml.scopedElement( "TestList" )
            .writeAttribute( "name", "Default test list for Catch2" )
            .writeAttribute( "id", m_defaultTestListId );
    }

    void VstestReporter::writeSummaryElement() {
        auto resultSummaryElement = m_xml.scopedElement( "ResultSummary" );

        auto hasFailures = false;
        for ( auto const& testEntry : m_testEntries ) {
            if ( testEntry.hasFailures() ) {
                hasFailures = true;
                break;
            }
        }
        resultSummaryElement.writeAttribute(
            "outcome", hasFailures ? "Failed" : "Passed" );

        if ( !m_config->attachment().empty() ) {
            auto resultFilesElement = m_xml.scopedElement( "ResultFiles" );
            m_xml.scopedElement( "ResultFile" )
                .writeAttribute( "path", m_config->attachment() );
        }
    }

    void VstestReporter::emitTrx() {
        startTestRunElement();
        writeTimesElement();
        writeResults();
        writeTestDefinitions();
        writeTestEntries();
        writeTestLists();
        writeSummaryElement();
        m_xml.endElement(); // TestRun
    }

    void VstestReporter::flushCurrentUnwindContext(
        const SectionStats* lastSectionStats ) {
        if ( m_currentUnwindContext.allSectionStats.empty() ) {
            return;
        }

        if ( lastSectionStats != nullptr ) {
            auto&& lastSectionInfo = lastSectionStats->sectionInfo;
            // Info messages and other input will show up *before* sections
            // get to serialize their redirected data. Delineate when we're
            // appending in this manner to avoid the perception of timing
            // mismatch (all info messages will show up first, not
            // interleaved with printf output, as an example)
            if ( !m_currentUnwindContext.stdOut.empty() &&
                 !lastSectionInfo.stdOut.empty() ) {
                m_currentUnwindContext.stdOut +=
                    "--- full standard output follows ---\n";
            }
            m_currentUnwindContext.stdOut += lastSectionInfo.stdOut;
            if ( !m_currentUnwindContext.stdErr.empty() &&
                 !lastSectionInfo.stdErr.empty() ) {
                m_currentUnwindContext.stdErr +=
                    "--- full standard error output follows ---\n";
            }
            m_currentUnwindContext.stdErr += lastSectionInfo.stdErr;
        }

        m_currentUnwindContext.elapsedNanoseconds =
            m_timer.getElapsedNanoseconds();
        auto&& currentStats = m_currentUnwindContext.allSectionStats;
        auto&& currentToplevelName =
            currentStats[currentStats.size() - 1].sectionInfo.name;
        bool isUnwindFromCurrentTest =
            !m_testEntries.empty() &&
            m_testEntries[m_testEntries.size() - 1].name ==
                getSanitizedTrxName( currentToplevelName );

        if ( !isUnwindFromCurrentTest ) {
            VstestEntry newEntry{ currentToplevelName };
            newEntry.tags = m_currentTestCaseTags;
            m_testEntries.push_back( std::move( newEntry ) );
        }

        m_currentUnwindContext.endTimestamp = currentTimestamp();
        auto&& currentTest = m_testEntries[m_testEntries.size() - 1];
        currentTest.unwindContexts.push_back(
            std::move( m_currentUnwindContext ) );

        m_currentUnwindContext.clear();
    }
    CATCH_REGISTER_REPORTER( "vstest", VstestReporter )

} // end namespace Catch
