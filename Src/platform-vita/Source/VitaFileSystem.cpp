/*
 * VitaFileSystem.cpp — file I/O for the AGK Vita port.
 *
 * The cFile / cFolder / cFileEntry classes are platform-specific in AGK
 * (each platform's *Core file implements them). The Vita filesystem is
 * POSIX-ish through vitasdk's newlib, so the bulk of this is copied verbatim
 * from LinuxCore.cpp — it is pure <stdio.h> / <sys/stat.h>.
 *
 * The only Vita-specific divergence is directory traversal: vitasdk's
 * `struct dirent` has no `d_type` field — it carries a `d_stat` (SceIoStat)
 * instead, so DT_DIR/DT_REG checks become SCE_S_ISDIR / SCE_S_ISREG.
 */

#include "agk.h"

#include <sys/stat.h>
#include <dirent.h>
#include <utime.h>
#include <psp2common/kernel/iofilemgr.h>

using namespace AGK;

namespace AGK {
    char *AGK_Vita_GetReadDir();
    char *AGK_Vita_GetWriteDir();
}

/* ------------------------------------------------------------------ */
/* uString numeric conversions — platform layer owns these in AGK       */
/* ------------------------------------------------------------------ */
float uString::ToFloat() const
{
    if ( !m_pData || !*m_pData ) return 0;
    return (float)atof(m_pData);
}

int uString::ToInt() const
{
    if ( !m_pData || !*m_pData ) return 0;
    return atoi(m_pData);
}

/* AGKFont — Vita has no queryable system font directory. */
int AGKFont::PlatformGetSystemFontPath( const uString & /*sFontName*/, uString & /*sOut*/ )
{
    return 0;
}

/* ------------------------------------------------------------------ */
/* cFile — verbatim from LinuxCore.cpp (pure stdio, portable)          */
/* ------------------------------------------------------------------ */
bool AGK::cFile::ExistsWrite( const char *szFilename )
{
	if ( !szFilename || !*szFilename ) return false;
	if ( strncmp(szFilename, "raw:", 4) == 0 ) return false;

	if ( strchr( szFilename, ':' ) ) return false;
	if ( strstr(szFilename, "..\\") || strstr(szFilename, "../") ) return false;

	uint32_t length = strlen(szFilename);
	if ( szFilename[length-1] == '/' || szFilename[length-1] == '\\' ) return false;

	uString sPath( szFilename );
	agk::PlatformGetFullPathWrite( sPath );

	struct stat buf;
	if ( stat( sPath.GetStr(), &buf ) != 0 ) return false;

	return true;
}

bool AGK::cFile::ExistsRead( const char *szFilename, int *mode )
{
	if ( !szFilename || !*szFilename ) return false;
	if ( strncmp(szFilename, "raw:", 4) == 0 ) return false;

	if ( strchr( szFilename, ':' ) ) return false;
	if ( strstr(szFilename, "..\\") || strstr(szFilename, "../") ) return false;

	uint32_t length = strlen(szFilename);
	if ( szFilename[length-1] == '/' || szFilename[length-1] == '\\' ) return false;

	uString sPath( szFilename );
	agk::PlatformGetFullPathRead( sPath );

	struct stat buf;
	if ( stat( sPath.GetStr(), &buf ) != 0 ) return false;

	return true;
}

bool cFile::ExistsRaw( const char *szFilename )
{
	if ( !szFilename || !*szFilename ) return false;
	uint32_t length = strlen(szFilename);
	if ( szFilename[length-1] == '/' || szFilename[length-1] == '\\' ) return false;

	if ( strncmp(szFilename, "raw:", 4) != 0 ) return false;
	if ( !agk::IsAbsolutePath( szFilename ) ) return false;

	// absolute path to anywhere allowed
	struct stat buf;
	if ( stat( szFilename+4, &buf ) != 0 ) return false;
	return true;
}

bool AGK::cFile::Exists( const char *szFilename )
{
	if ( !ExistsRaw( szFilename ) )
	{
		if ( !ExistsWrite( szFilename ) )
		{
			if ( !ExistsRead( szFilename ) ) return false;
		}
	}

	return true;
}

bool cFile::GetModified( const char *szFilename, int &time )
{
	int64_t time64 = 0;
	bool result = GetModified64( szFilename, time64 );
	time = (int) time64;
	return result;
}

bool cFile::GetModified64( const char *szFilename, int64_t &time )
{
	time = 0;
	if ( !szFilename || !*szFilename ) return false;

	uint32_t length = strlen(szFilename);
	if ( szFilename[length-1] == '/' || szFilename[length-1] == '\\' ) return false;

	uString sPath( szFilename );
	if ( !agk::GetRealPath( sPath ) ) return false;

	struct stat fileInfo;
	int result = stat( sPath, &fileInfo );
	if ( result != 0 )
	{
		return false;
	}

	time = (int64_t) fileInfo.st_mtime;
	return true;
}

void cFile::SetModified( const char *szFilename, int time )
{
	if ( !szFilename || !*szFilename ) return;
	uint32_t length = strlen(szFilename);
	if ( szFilename[length-1] == '/' || szFilename[length-1] == '\\' ) return;

	uString sPath( szFilename );
	if ( cFile::ExistsRaw( szFilename ) ) sPath.SetStr( szFilename+4 );
	else if ( cFile::ExistsWrite( szFilename ) ) agk::PlatformGetFullPathWrite(sPath);
	else return;

	struct utimbuf fileInfo;
	fileInfo.actime = time;
	fileInfo.modtime = time;
	utime( sPath.GetStr(), &fileInfo );
}

uint32_t AGK::cFile::GetFileSize( const char *szFilename )
{
	cFile pFile;
	if ( !pFile.OpenToRead( szFilename ) ) return 0;
	uint32_t size = pFile.GetSize();
	pFile.Close();

	return size;
}

void AGK::cFile::DeleteFile( const char *szFilename )
{
	if ( !szFilename || !*szFilename ) return;

	uint32_t length = strlen(szFilename);
	if ( szFilename[length-1] == '/' || szFilename[length-1] == '\\' )
	{
		agk::Error( "Invalid path for DeleteFile file, must not end in a forward or backward slash" );
		return;
	}

	uString sPath( szFilename );
	if ( cFile::ExistsRaw( szFilename ) ) sPath.SetStr( szFilename+4 );
	else if ( cFile::ExistsWrite( szFilename ) ) agk::PlatformGetFullPathWrite(sPath);
	else return;

	remove( sPath.GetStr() );
	agk::m_bUpdateFileLists = true;
}

bool AGK::cFile::OpenToWrite( const char *szFilename, bool append )
{
	if ( !szFilename || !*szFilename ) return false;
	if ( pFile ) Close();
	mode = 1;

	int raw = 0;
	uString sPath( szFilename );
	if ( strncmp(szFilename, "raw:", 4) == 0 )
	{
		raw = 1;
		sPath.SetStr( szFilename+4 );
	}
	else agk::PlatformGetFullPathWrite(sPath);

	if ( !agk::PlatformCreateRawPath( sPath ) ) return false;

	pFilePtr = 0;
	if ( append ) pFile = AGKfopen( sPath.GetStr(), "ab" );
	else pFile = AGKfopen( sPath.GetStr(), "wb" );

	if ( !pFile )
	{
		uString err = "Failed to open file for writing ";
		err += szFilename;
		agk::Error( err );
		return false;
	}

	// refresh any stored directory details for the new file
	if ( 0 == raw ) cFileEntry::AddNewFile( sPath.GetStr() );
	agk::m_bUpdateFileLists = true;

	return true;
}

bool AGK::cFile::OpenToRead( const char *szFilename )
{
	if ( !szFilename || !*szFilename ) return false;
	if ( pFile ) Close();
	mode = 0;

	uString sPath ( szFilename );
	if ( !agk::GetRealPath( sPath ) )
	{
		uString err = "Could not find file ";
		err += szFilename;
		agk::Error( err );
		return false;
	}

	/* Reject directories: opening one as a file gives garbage reads. */
	{
		struct stat dirCheck;
		if ( stat( sPath.GetStr(), &dirCheck ) == 0 && S_ISDIR( dirCheck.st_mode ) )
		{
			uString err = "Cannot open as a file, path is a directory: ";
			err += szFilename;
			agk::Error( err );
			return false;
		}
	}

	pFilePtr = 0;
	pFile = AGKfopen( sPath, "rb" );
	if ( !pFile )
	{
		uString err = "Failed to open file for reading ";
		err += szFilename;
		agk::Error( err );
		return false;
	}

	return true;
}

void AGK::cFile::Close()
{
	if ( pFile ) fclose( pFile );
	pFile = 0;
}

uint32_t AGK::cFile::GetPos()
{
	if ( !pFile ) return 0;

	return ftell( pFile );
}

void AGK::cFile::Seek( uint32_t pos )
{
	if ( !pFile ) return;
	fseek( pFile, pos, SEEK_SET );
}

void AGK::cFile::Flush()
{
	if ( !pFile ) return;
	fflush( pFile );
}

uint32_t AGK::cFile::GetSize()
{
	if ( !pFile ) return 0;
	fpos_t pos;
	fgetpos( pFile, &pos );
	fseek( pFile, 0, SEEK_END );
	long size = ftell( pFile );
	fsetpos( pFile, &pos );
	/* ftell gives -1 for non-regular files — don't let that wrap to a huge size. */
	if ( size < 0 ) return 0;
	return (uint32_t) size;
}

void AGK::cFile::Rewind()
{
	if ( !pFile ) return;
	rewind( pFile );
}

bool AGK::cFile::IsEOF()
{
	if ( !pFile ) return true;
	return feof( pFile ) != 0;
}

void cFile::WriteByte( unsigned char b )
{
	if ( !pFile ) return;
	if ( mode != 1 )
	{
#ifdef _AGK_ERROR_CHECK
		agk::Error( "Cannot write to file opened for reading" );
#endif
		return;
	}

	fwrite( &b, 1, 1, pFile );
}

void AGK::cFile::WriteInteger( int i )
{
	if ( !pFile ) return;
	if ( mode != 1 )
	{
		agk::Error( "Cannot write to file opened for reading" );
		return;
	}

	//convert everything to little endian for cross platform compatibility
	i = agk::PlatformLittleEndian( i );
	fwrite( &i, 4, 1, pFile );
}

void AGK::cFile::WriteFloat( float f )
{
	if ( !pFile ) return;
	if ( mode != 1 )
	{
		agk::Error( "Cannot write to file opened for reading" );
		return;
	}
	fwrite( &f, 4, 1, pFile );
}

void AGK::cFile::WriteString( const char *str )
{
	if ( !pFile ) return;
	if ( mode != 1 )
	{
		agk::Error( "Cannot write to file opened for reading" );
		return;
	}
	uint32_t length = strlen( str );
	fwrite( str, 1, length+1, pFile );
}

void cFile::WriteString2( const char *str )
{
	if ( !str ) return;
	if ( !pFile ) return;
	if ( mode != 1 )
	{
#ifdef _AGK_ERROR_CHECK
		agk::Error( "Cannot write to file opened for reading" );
#endif
		return;
	}
	uint32_t length = strlen( str );
	uint32_t l = agk::PlatformLittleEndian( length );
	fwrite( &l, 4, 1, pFile );
	fwrite( str, 1, length, pFile );
}

void AGK::cFile::WriteData( const char *str, uint32_t bytes )
{
	if ( !pFile ) return;
	if ( mode != 1 )
	{
		agk::Error( "Cannot write to file opened for reading" );
		return;
	}

	fwrite( str, 1, bytes, pFile );
}

void AGK::cFile::WriteLine( const char *str )
{
	if ( !pFile ) return;
	if ( mode != 1 )
	{
#ifdef _AGK_ERROR_CHECK
		agk::Error( "Cannot write to file opened for reading" );
#endif
		return;
	}
	uint32_t length = strlen( str );
	fwrite( str, 1, length, pFile );

	// strings terminate with CR (13,10) - so it resembles a regular text file when file viewed
	char pCR[2];
	pCR[0]=13;
	pCR[1]=10;
	fwrite( &pCR[0], 1, 1, pFile );
	fwrite( &pCR[1], 1, 1, pFile );
}

unsigned char cFile::ReadByte( )
{
	if ( !pFile ) return 0;
	if ( mode != 0 )
	{
#ifdef _AGK_ERROR_CHECK
		agk::Error( "Cannot read from file opened for writing" );
#endif
		return 0;
	}
	unsigned char b = 0;
	fread( &b, 1, 1, pFile );
	return b;
}

int AGK::cFile::ReadInteger( )
{
	if ( !pFile ) return 0;
	if ( mode != 0 )
	{
		agk::Error( "Cannot read from file opened for writing" );
		return 0;
	}
	int i = 0;
	fread( &i, 4, 1, pFile );
	// convert back to local endian, everything in the file is little endian.
	return i = agk::PlatformLocalEndian( i );
}

float AGK::cFile::ReadFloat( )
{
	if ( !pFile ) return 0;
	if ( mode != 0 )
	{
		agk::Error( "Cannot read from file opened for writing" );
		return 0;
	}
	float f;
	fread( &f, 4, 1, pFile );
	return f;
}

int AGK::cFile::ReadString( uString &str )
{
	if ( !pFile ) return 0;
	if ( mode != 0 )
	{
		agk::Error( "Cannot read from file opened for writing" );
		return 0;
	}

	char *buffer = 0;
	int bufLen = 0;
	int pos = 0;
	int diff = 0;

	// read until a LF (10) line terminator is found, or eof.
	do
	{
		pos = bufLen;
		if ( bufLen == 0 )
		{
			bufLen = 256;
			buffer = new char[257];
			diff = 256;
		}
		else
		{
			int newLen = bufLen*3 / 2;
			char *newBuf = new char[ newLen+1 ];
			memcpy( newBuf, buffer, bufLen );
			delete [] buffer;
			diff = newLen - bufLen;
			buffer = newBuf;
			bufLen = newLen;
		}

		long lPos = ftell( pFile );
		uint32_t written = (uint32_t) fread( buffer+pos, 1, diff, pFile );
		buffer[pos+written] = 0;
		bool bFound = false;
		for ( uint32_t i = 0; i < written; i++ )
		{
			if ( buffer[ pos+i ] == 0 )
			{
				fseek( pFile, lPos+i+1, SEEK_SET );
				bFound = true;
				break;
			}
		}
		if ( bFound ) break;
	} while( !feof( pFile ) );

	str.SetStr( buffer );
	delete [] buffer;
	return str.GetLength();
}

int cFile::ReadString2( uString &str )
{
	if ( !pFile )
	{
#ifdef _AGK_ERROR_CHECK
		agk::Error( "Cannot read from file, file not open" );
#endif
		return -1;
	}

	if ( mode != 0 )
	{
#ifdef _AGK_ERROR_CHECK
		agk::Error( "Cannot read from file opened for writing" );
#endif
		return -1;
	}

	str.ClearTemp();

	uint32_t length = 0;
	if ( fread( &length, 4, 1, pFile ) != 1 ) return 0;
	// convert back to local endian, everything in the file is little endian.
	length = agk::PlatformLocalEndian( length );

	/* Guard against a bogus length prefix from a corrupt file. */
	long cur = ftell( pFile );
	fseek( pFile, 0, SEEK_END );
	long end = ftell( pFile );
	if ( cur >= 0 ) fseek( pFile, cur, SEEK_SET );
	if ( cur < 0 || end < 0 || length > (uint32_t)( end - cur ) )
	{
#ifdef _AGK_ERROR_CHECK
		agk::Error( "Corrupt file: string length exceeds remaining data" );
#endif
		return 0;
	}

	char *buffer = new char[ length+1 ];
	uint32_t got = (uint32_t) fread( buffer, 1, length, pFile );
	buffer[ got ] = 0;
	str.SetStr( buffer );

	delete [] buffer;

	return str.GetLength();
}

int AGK::cFile::ReadLine( uString &str )
{
	if ( !pFile )
	{
#ifdef _AGK_ERROR_CHECK
		agk::Error( "Cannot read from file, file not open" );
#endif
		return -1;
	}

	if ( mode != 0 )
	{
#ifdef _AGK_ERROR_CHECK
		agk::Error( "Cannot read from file opened for writing" );
#endif
		return -1;
	}

	str.ClearTemp();

	char *buffer = 0;
	int bufLen = 0;
	int pos = 0;
	int diff = 0;

	// read until a LF (10) line terminator is found, or eof.
	do
	{
		pos = bufLen;
		if ( bufLen == 0 )
		{
			bufLen = 256;
			buffer = new char[257];
			diff = 256;
		}
		else
		{
			int newLen = bufLen*3 / 2;
			char *newBuf = new char[ newLen+1 ];
			memcpy( newBuf, buffer, bufLen );
			delete [] buffer;
			diff = newLen - bufLen;
			buffer = newBuf;
			bufLen = newLen;
		}

		long lPos = ftell( pFile );
		uint32_t written = (uint32_t) fread( buffer+pos, 1, diff, pFile );
		buffer[pos+written] = 0;
		bool bFound = false;
		for ( uint32_t i = 0; i < written; i++ )
		{
			if ( buffer[ pos+i ] == '\n' )
			{
				buffer[ pos+i ] = 0;
				fseek( pFile, lPos+i+1, SEEK_SET );
				bFound = true;
				break;
			}
		}
		if ( bFound ) break;
	} while( !feof( pFile ) );

	str.SetStr( buffer );
	delete [] buffer;
	str.Trim( "\r\n" );
	return str.GetLength();
}

int AGK::cFile::ReadData( char *str, uint32_t length )
{
	if ( !pFile ) return 0;
	if ( mode != 0 )
	{
		agk::Error( "Cannot read from file opened for writing" );
		return 0;
	}

	/* Retry transient short reads (flaky storage) before giving up. */
	uint32_t total = 0;
	for ( int attempt = 0; attempt < 4 && total < length; attempt++ )
	{
		total += (uint32_t) fread( str + total, 1, length - total, pFile );
		if ( total >= length || feof( pFile ) ) break;
		clearerr( pFile );   /* transient I/O error — clear it and retry */
	}
	return (int) total;
}

/* ------------------------------------------------------------------ */
/* cFolder — directory listing. Rewritten for vitasdk dirent (d_stat). */
/* ------------------------------------------------------------------ */
int cFolder::OpenFolder( const char* szPath )
{
	if ( m_pFiles ) delete [] m_pFiles;
	m_pFiles = 0;
	m_iNumFiles = 0;

	if ( m_pFolders ) delete [] m_pFolders;
	m_pFolders = 0;
	m_iNumFolders = 0;

	if ( strncmp( szPath, "raw:", 4 ) == 0 ) szPath += 4;

	if ( !agk::IsAbsolutePath( szPath ) )
	{
		uString err; err.Format( "Failed to open folder \"%s\", it must be an absolute path", szPath );
		agk::Error( err );
		return 0;
	}

	DIR *pDir = opendir( szPath );
	if ( !pDir )
	{
		uString err; err.Format( "Failed to open folder \"%s\", it may not exist", szPath );
		agk::Error( err );
		return 0;
	}

	dirent* item = readdir( pDir );
	while( item )
	{
		if ( SCE_S_ISDIR( item->d_stat.st_mode ) )
		{
			if ( strcmp( item->d_name, "." ) != 0 && strcmp( item->d_name, ".." ) != 0 )
				m_iNumFolders++;
		}
		else
		{
			m_iNumFiles++;
		}
		item = readdir( pDir );
	}
	closedir( pDir );

	m_pFiles = new uString[ m_iNumFiles ];
	m_pFolders = new uString[ m_iNumFolders ];

	int fileCount = 0;
	int folderCount = 0;

	pDir = opendir( szPath );
	item = readdir( pDir );
	while( item )
	{
		if ( SCE_S_ISDIR( item->d_stat.st_mode ) )
		{
			if ( strcmp( item->d_name, "." ) != 0 && strcmp( item->d_name, ".." ) != 0 )
				m_pFolders[ folderCount++ ].SetStr( item->d_name );
		}
		else
		{
			m_pFiles[ fileCount++ ].SetStr( item->d_name );
		}
		item = readdir( pDir );
	}
	closedir( pDir );

	return 1;
}

/* ------------------------------------------------------------------ */
/* cFileEntry — recursive file index. Rewritten for vitasdk dirent.    */
/* ------------------------------------------------------------------ */
void cFileEntry::TraverseDirectory( const char* dir )
{
	DIR *pDir = opendir( dir );
	if ( !pDir ) return;

	dirent* item = readdir( pDir );
	while( item )
	{
		if ( SCE_S_ISDIR( item->d_stat.st_mode ) )
		{
			if ( strcmp( item->d_name, "." ) != 0 && strcmp( item->d_name, ".." ) != 0 )
			{
				char str[ 1024 ];
				strcpy( str, dir );
				strcat( str, item->d_name );
				AddNewFile( str );

				strcat( str, "/" );
				TraverseDirectory( str );
			}
		}
		else
		{
			char str[ 1024 ];
			strcpy( str, dir );
			strcat( str, item->d_name );
			AddNewFile( str );
		}
		item = readdir( pDir );
	}
	closedir( pDir );
}

void cFileEntry::InitFileList()
{
	TraverseDirectory( AGK_Vita_GetReadDir() );
	TraverseDirectory( AGK_Vita_GetWriteDir() );
}
