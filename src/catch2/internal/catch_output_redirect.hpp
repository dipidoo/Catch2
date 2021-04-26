
//              Copyright Catch2 Authors
// Distributed under the Boost Software License, Version 1.0.
//   (See accompanying file LICENSE_1_0.txt or copy at
//        https://www.boost.org/LICENSE_1_0.txt)

// SPDX-License-Identifier: BSL-1.0
#ifndef CATCH_OUTPUT_REDIRECT_HPP_INCLUDED
#define CATCH_OUTPUT_REDIRECT_HPP_INCLUDED

#include <catch2/internal/catch_platform.hpp>
#include <catch2/internal/catch_stream.hpp>

#include <cstdio>
#include <iosfwd>
#include <string>

namespace Catch {

    class RedirectedStream {
        std::ostream& m_originalStream;
        std::ostream& m_redirectionStream;
        std::streambuf* m_prevBuf;

    public:
        RedirectedStream( std::ostream& originalStream, std::ostream& redirectionStream );
        ~RedirectedStream();
    };

    class RedirectedStdOut {
        ReusableStringStream m_rss;
        RedirectedStream m_cout;
    public:
        RedirectedStdOut();
        auto str() const -> std::string;
    };

    // StdErr has two constituent streams in C++, std::cerr and std::clog
    // This means that we need to redirect 2 streams into 1 to keep proper
    // order of writes
    class RedirectedStdErr {
        ReusableStringStream m_rss;
        RedirectedStream m_cerr;
        RedirectedStream m_clog;
    public:
        RedirectedStdErr();
        auto str() const -> std::string;
    };

    class RedirectedStreams {
    public:
        RedirectedStreams(RedirectedStreams const&) = delete;
        RedirectedStreams& operator=(RedirectedStreams const&) = delete;
        RedirectedStreams(RedirectedStreams&&) = delete;
        RedirectedStreams& operator=(RedirectedStreams&&) = delete;

        RedirectedStreams(std::string& redirectedCout, std::string& redirectedCerr);
        ~RedirectedStreams();
    private:
        std::string& m_redirectedCout;
        std::string& m_redirectedCerr;
        RedirectedStdOut m_redirectedStdOut;
        RedirectedStdErr m_redirectedStdErr;
    };

#if defined(CATCH_CONFIG_EXPERIMENTAL_REDIRECT)

    // Windows's implementation of std::tmpfile is terrible (it tries
    // to create a file inside system folder, thus requiring elevated
    // privileges for the binary), so we have to use tmpnam(_s) and
    // create the file ourselves there.
    class TempFile {
    public:
        TempFile(TempFile const&) = delete;
        TempFile& operator=(TempFile const&) = delete;
        TempFile(TempFile&&) = delete;
        TempFile& operator=(TempFile&&) = delete;

        TempFile(std::string filePath = "");
        ~TempFile();

        void reopen();
        std::FILE* getFile();
        std::string getPath();
        std::string getContents( int startPosition );

    private:
        std::FILE* m_file = nullptr;
        std::string m_filePath;
        bool m_shouldAutomaticallyDelete;
    };

    class OutputRedirectSink {
    public:
        OutputRedirectSink( OutputRedirectSink const& ) = delete;
        OutputRedirectSink& operator=( OutputRedirectSink const& ) = delete;
        OutputRedirectSink( OutputRedirectSink&& ) = delete;
        OutputRedirectSink& operator=( OutputRedirectSink&& ) = delete;

        OutputRedirectSink( FILE* redirectionSource,
                            std::string redirectionDestination = "" );
        ~OutputRedirectSink();

        std::string getContentsFromPosition( int position );
        std::string getAllContents();
        std::string getLatestContents();

        void reset();

    private:
        FILE* m_originalSource;
        int m_originalSourceDescriptor;
        int m_originalSourceCopyDescriptor;
        size_t m_lastGetPosition;
        TempFile m_tempFile;
    };

    class OutputRedirect {
    public:
        OutputRedirect(OutputRedirect const&) = delete;
        OutputRedirect& operator=(OutputRedirect const&) = delete;
        OutputRedirect(OutputRedirect&&) = delete;
        OutputRedirect& operator=(OutputRedirect&&) = delete;

        OutputRedirect(std::string& stdout_dest, std::string& stderr_dest);
        ~OutputRedirect();

    private:
        OutputRedirectSink m_stdOutRedirect;
        OutputRedirectSink m_stdErrRedirect;
        std::string& m_stdoutDest;
        std::string& m_stderrDest;
    };

#endif

} // end namespace Catch

#endif // CATCH_OUTPUT_REDIRECT_HPP_INCLUDED
