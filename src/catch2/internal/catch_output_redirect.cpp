
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

#if defined(CATCH_CONFIG_EXPERIMENTAL_REDIRECT)
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

#if defined(CATCH_CONFIG_EXPERIMENTAL_REDIRECT)

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
#ifdef _MSC_VER
            if ( fopen_s( &m_file, m_filePath.c_str(), "w+" ) ) {
                CATCH_RUNTIME_ERROR( "Failed to open file: " << m_filePath.c_str() );
            }
#else
            m_file = std::fopen( m_filePath.c_str(), "w+" );
#endif
        } else {
#ifdef _MSC_VER
            char tempNameBuffer[L_tmpnam_s];
            if ( tmpnam_s( tempNameBuffer ) ) {
                CATCH_RUNTIME_ERROR( "Failed to acquire a temporary file name." );
            }
            m_filePath = tempNameBuffer;
            if ( fopen_s( &m_file, m_filePath.c_str(), "w+" ) ) {
                CATCH_RUNTIME_ERROR( "Failed to open file: " << m_filePath.c_str() );
            }
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

    std::string TempFile::getContents( int startPosition ) {
        fflush(m_file);
        std::stringstream sstr;
        char buffer[100] = {};
        std::fseek( m_file, startPosition, SEEK_SET );
        while (std::fgets(buffer, sizeof(buffer), m_file)) {
            sstr << buffer;
        }
        return sstr.str();
    }

    OutputRedirectSink::OutputRedirectSink(
        FILE* redirectionSource,
        std::string redirectionTemporaryFilePath ) 
        : m_originalSource( redirectionSource ),
        m_lastGetPosition( 0 ),
        m_tempFile( redirectionTemporaryFilePath )
    {
        // Disable buffering for the redirection stream -- this will persist even after the
        // redirection completes!
        setvbuf( redirectionSource, NULL, _IONBF, 0 );

        m_originalSourceDescriptor = fileno( redirectionSource );
        m_originalSourceCopyDescriptor = dup( m_originalSourceDescriptor );
        (void)dup2( fileno( m_tempFile.getFile() ), m_originalSourceDescriptor );
    }

    OutputRedirectSink::~OutputRedirectSink() {
        (void)dup2( m_originalSourceCopyDescriptor, m_originalSourceDescriptor );
    }

    std::string OutputRedirectSink::getContentsFromPosition( int position ) {
        fflush( m_originalSource );
        auto result = m_tempFile.getContents( position );
        m_lastGetPosition = position + result.size();
        return result;
    }

    std::string OutputRedirectSink::getAllContents() {
        return getContentsFromPosition( 0 );
    }

    std::string OutputRedirectSink::getLatestContents() {
        return getContentsFromPosition( m_lastGetPosition );
    }

    void OutputRedirectSink::reset() {
        (void)dup2( m_originalSourceCopyDescriptor, m_originalSourceDescriptor );
        m_tempFile.reopen();
        (void)dup2( fileno( m_tempFile.getFile() ), m_originalSourceDescriptor );
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

        m_stdoutDest += m_stdOutRedirect.getAllContents();
        m_stderrDest += m_stdErrRedirect.getAllContents();
    }

#endif // CATCH_CONFIG_EXPERIMENTAL_REDIRECT

} // namespace Catch

#if defined(CATCH_CONFIG_EXPERIMENTAL_REDIRECT)
    #if defined(_MSC_VER)
    #undef dup
    #undef dup2
    #undef fileno
    #endif
#endif
