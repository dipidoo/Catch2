
//              Copyright Catch2 Authors
// Distributed under the Boost Software License, Version 1.0.
//   (See accompanying file LICENSE_1_0.txt or copy at
//        https://www.boost.org/LICENSE_1_0.txt)

// SPDX-License-Identifier: BSL-1.0
#include <catch2/internal/catch_output_redirect.hpp>
#include <catch2/internal/catch_enforce.hpp>

#include <cstdio>
#include <cstring>
#include <sstream>

#if defined(CATCH_CONFIG_NEW_CAPTURE)
    #if defined(_MSC_VER)
    #include <io.h>      //_dup and _dup2
    #define dup _dup
    #define dup2 _dup2
    #define fileno _fileno
    #else
    #include <unistd.h>  // dup and dup2
    #endif
#endif


namespace Catch {

    RedirectedStream::RedirectedStream( std::ostream& originalStream, std::ostream& redirectionStream )
    :   m_originalStream( originalStream ),
        m_redirectionStream( redirectionStream ),
        m_prevBuf( m_originalStream.rdbuf() )
    {
        m_originalStream.rdbuf( m_redirectionStream.rdbuf() );
    }

    RedirectedStream::~RedirectedStream() {
        m_originalStream.rdbuf( m_prevBuf );
    }

    RedirectedStdOut::RedirectedStdOut() : m_cout( Catch::cout(), m_rss.get() ) {}
    auto RedirectedStdOut::str() const -> std::string { return m_rss.str(); }

    RedirectedStdErr::RedirectedStdErr()
    :   m_cerr( Catch::cerr(), m_rss.get() ),
        m_clog( Catch::clog(), m_rss.get() )
    {}
    auto RedirectedStdErr::str() const -> std::string { return m_rss.str(); }

    RedirectedStreams::RedirectedStreams(std::string& redirectedCout, std::string& redirectedCerr)
    :   m_redirectedCout(redirectedCout),
        m_redirectedCerr(redirectedCerr)
    {}

    RedirectedStreams::~RedirectedStreams() {
        m_redirectedCout += m_redirectedStdOut.str();
        m_redirectedCerr += m_redirectedStdErr.str();
    }

#if defined(CATCH_CONFIG_NEW_CAPTURE)

    TempFile::TempFile(std::string filePath):
        m_filePath{ filePath },
        m_shouldAutomaticallyDelete{ false } {

        reopen();
    }

    void TempFile::reopen() {
        if ( m_file ) {
            fclose( m_file );
            m_file = nullptr;
        }
        if ( !m_filePath.empty() ) {
            m_file = std::fopen( m_filePath.c_str(), "w+" );
        } else {
            #if defined( _MSC_VER )
            char tempNameBuffer[L_tmpnam_s];
            if ( tmpnam_s( tempNameBuffer ) ) {
                CATCH_RUNTIME_ERROR( "Failed to acquire a temporary file name." );
            }
            m_file = std::fopen( tempNameBuffer, "w+" );
            m_filePath = tempNameBuffer;
            m_shouldAutomaticallyDelete = true;
            #else
            m_file = std::tmpfile();
            #endif
        }
    }

    TempFile::~TempFile() {
        // TBD: What to do about errors here?
        std::fclose(m_file);

        if (m_shouldAutomaticallyDelete) {
            std::remove(m_filePath.c_str());
        }
    }

    std::string TempFile::getPath() {
        return m_filePath;
    }

    FILE* TempFile::getFile() {
        return m_file;
    }

    std::string TempFile::getContents() {
        fflush(m_file);
        std::stringstream sstr;
        char buffer[100] = {};
        std::rewind(m_file);
        while (std::fgets(buffer, sizeof(buffer), m_file)) {
            sstr << buffer;
        }
        return sstr.str();
    }

    OutputRedirectSink::OutputRedirectSink(
        FILE* redirectionSource,
        std::string redirectionTemporaryFilePath ) 
        : m_originalSource( redirectionSource )
        , m_tempFile( redirectionTemporaryFilePath )
    {
        // Disable buffering for the redirection stream -- this will persist even after the
        // redirection completes!
        setvbuf( redirectionSource, NULL, _IONBF, 0 );

        m_originalSourceDescriptor = fileno( redirectionSource );
        m_originalSourceCopyDescriptor = dup( m_originalSourceDescriptor );
        dup2( fileno( m_tempFile.getFile() ), m_originalSourceDescriptor );
    }

    OutputRedirectSink::~OutputRedirectSink() {
        dup2( m_originalSourceCopyDescriptor, m_originalSourceDescriptor );
    }

    std::string OutputRedirectSink::getContents() {
        fflush( m_originalSource );
        return m_tempFile.getContents();
    }

    void OutputRedirectSink::reset() {
        dup2( m_originalSourceCopyDescriptor, m_originalSourceDescriptor );
        m_tempFile.reopen();
        dup2( fileno( m_tempFile.getFile() ), m_originalSourceDescriptor );
    }

    OutputRedirect::OutputRedirect(std::string& stdout_dest, std::string& stderr_dest) :
        m_stdOutRedirect(stdout),
        m_stdErrRedirect(stderr),
        m_stdoutDest(stdout_dest),
        m_stderrDest(stderr_dest) {
    }

    OutputRedirect::~OutputRedirect() {
        // Since we support overriding these streams, we flush cerr
        // even though std::cerr is unbuffered
        Catch::cout() << std::flush;
        Catch::cerr() << std::flush;
        Catch::clog() << std::flush;

        m_stdoutDest += m_stdOutRedirect.getContents();
        m_stderrDest += m_stdErrRedirect.getContents();
    }

#endif // CATCH_CONFIG_NEW_CAPTURE

} // namespace Catch

#if defined(CATCH_CONFIG_NEW_CAPTURE)
    #if defined(_MSC_VER)
    #undef dup
    #undef dup2
    #undef fileno
    #endif
#endif
