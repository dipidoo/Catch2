
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

#if defined(_MSC_VER)
    TempFile::TempFile(std::string filePath) {
        if ( !filePath.empty() ) {
            const auto arraySize = sizeof( m_buffer ) / sizeof( m_buffer[0] );
            if ( filePath.copy( m_buffer, arraySize ) != filePath.length() ) {
                CATCH_RUNTIME_ERROR(
                    "Provided temporary file path was too long to copy" );
            }
        }
        else if (tmpnam_s(m_buffer)) {
            CATCH_RUNTIME_ERROR("Could not get a temp filename");
        }

        reopen();
    }

    void TempFile::reopen() {
        if ( m_file ) {
            fclose( m_file );
            m_file = nullptr;
        }
        if ( fopen_s( &m_file, m_buffer, "w+" ) ) {
            char buffer[100];
            if ( strerror_s( buffer, errno ) ) {
                CATCH_RUNTIME_ERROR( "Could not translate errno to a string" );
            }
            CATCH_RUNTIME_ERROR( "Could not open the temp file: '"
                                 << m_buffer << "' because: " << buffer );
        }
    }

    std::string TempFile::getPath() {
        return m_buffer;
    }

#else
    TempFile::TempFile(std::string filePath) {
        m_filePath = filePath;
        reopen();
    }

    void TempFile::reopen() {
        if ( m_file ) {
            std::fclose( m_file );
            m_file = nullptr;
        }
        m_file = m_filePath.empty() ? std::tmpfile() : std::fopen( m_filePath.c_str(), "w+" );
        if (!m_file) {
            CATCH_RUNTIME_ERROR("Could not create a temp file.");
        }
    }

#endif

    TempFile::~TempFile() {
         // TBD: What to do about errors here?
         std::fclose(m_file);
         // We manually create the file on Windows only, on Linux
         // it will be autodeleted
#if defined(_MSC_VER)
         std::remove(m_buffer);
#endif
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
