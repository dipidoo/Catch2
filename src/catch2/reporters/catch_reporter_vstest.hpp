
//              Copyright Catch2 Authors
// Distributed under the Boost Software License, Version 1.0.
//   (See accompanying file LICENSE_1_0.txt or copy at
//        https://www.boost.org/LICENSE_1_0.txt)

// SPDX-License-Identifier: BSL-1.0#ifndef CATCH_REPORTER_VSTEST_HPP_INCLUDED
#ifndef CATCH_REPORTER_VSTEST_HPP_INCLUDED
#define CATCH_REPORTER_VSTEST_HPP_INCLUDED

#include <catch2/catch_test_case_info.hpp>
#include <catch2/internal/catch_xmlwriter.hpp>
#include <catch2/reporters/catch_reporter_incremental_base.hpp>

#ifdef __clang__
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Wpadded"
#endif

namespace Catch {
    using SectionTraversalRef = IncrementalReporterBase::SectionTraversalRef;

    class VstestResult {
    public:
        static std::vector<VstestResult>
        parseTraversals( const std::vector<SectionTraversalRef>& traversals );

        const std::string testId;
        const std::string testExecutionId;
        std::vector<SectionTraversalRef> traversals;

        bool isOk() const;

        std::string getRootTestName() const;
        std::string getRootRunName() const;
        const std::vector<Catch::Tag> getRootTestTags() const;

        std::chrono::system_clock::time_point getStartTime() const;
        std::chrono::system_clock::time_point getFinishTime() const;

    private:
        VstestResult();
    };

    class VstestTrxDocument {
    public:
        static void serialize( std::ostream& stream,
                               std::vector<VstestResult>& results,
                               const std::string& sourcePrefix,
                               const std::vector<std::string>& attachmentPaths,
                               bool usePerTraversalOutputRedirection );

    private:
        VstestTrxDocument( std::ostream& stream,
                           std::vector<VstestResult>& results,
                           const std::string& sourcePrefix,
                           const std::vector<std::string>& attachmentPaths,
                           bool usePerTraversalOutputRedirection );

    public:
        enum class EmitOutput {
            Stack = 1 << 0,
            Message = 1 << 1,
            StdOut = 1 << 2,
            StdErr = 1 << 3,
            Everything = 0xFFFF,
        };

    private:
        void startWriteTestRun();
        void writeTimes();
        void writeResults();
        void writeTopLevelResult( const VstestResult& result );
        void writeTimestampAttributes(
            std::chrono::system_clock::time_point start,
            std::chrono::system_clock::time_point finish );
        void startWriteTestResult( const VstestResult& result );
        void startWriteTestResult( const std::string& testId,
                                   const std::string& testExecutionId,
                                   const std::string& testName );
        void writeTraversalOutput( IncrementalSectionTraversal& traversal,
                                   EmitOutput includeOptions );
        void writeInnerResult( const VstestResult& result,
                               IncrementalSectionTraversal& traversal );
        void writeTestDefinitions();
        void writeTestLists();
        void writeTestEntries();
        void writeSummary( const std::vector<std::string>& attachmentPaths );

    private:
        static std::string
        getNormalizedPath( const std::string& unnormalizedPrefix );
        void serializeSourceInfo( std::ostringstream& stream,
                                  const std::string& file,
                                  const size_t line );
        std::string
        getErrorMessageForTraversal( IncrementalSectionTraversal& traversal );
        std::string
        getStackMessageForTraversal( IncrementalSectionTraversal& traversal );
        std::string
        getFullTestNameForTraversal( IncrementalSectionTraversal& traversal );

    private:
        XmlWriter m_xml;
        bool m_usePerTraversalOutputRedirection;
        const std::vector<VstestResult> m_results;
        const std::string m_sourcePrefix;
        const std::vector<std::string> m_attachmentPaths;
        const std::string m_defaultTestListId;
    };

    class VstestReporter : public IncrementalReporterBase {
    public:
        VstestReporter( ReporterConfig const& _config );
        ~VstestReporter() override {}

        static std::string getDescription();

    protected:
        void sectionStarting( SectionInfo const& sectionInfo ) override;
        void sectionTraversalEnded(
            std::vector<SectionTraversalRef> traversals ) override;
        void testRunEnded( const Catch::TestRunStats& testStats ) override;

    private:
        void emitNewTrx( const std::vector<SectionTraversalRef>& traversals );
    };

    VstestTrxDocument::EmitOutput
    operator|( VstestTrxDocument::EmitOutput lhs,
               VstestTrxDocument::EmitOutput rhs );
    VstestTrxDocument::EmitOutput
    operator&( VstestTrxDocument::EmitOutput lhs,
               VstestTrxDocument::EmitOutput rhs );

} // end namespace Catch

#ifdef __clang__
#    pragma clang diagnostic pop
#endif

#endif // CATCH_REPORTER_VSTEST_HPP_INCLUDED