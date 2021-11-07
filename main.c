#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <netinet/in.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h> 


#define MSG_FATAL( ... ) fprintf( stderr, "fatal: " __VA_ARGS__ )
#define MSG_ERROR( ... ) fprintf( stderr, "error: " __VA_ARGS__ )

#define MSG_WARN( ... ) fprintf( stderr, "warning: " __VA_ARGS__ )
#define MSG_NOTE( ... ) printf( "notice: " __VA_ARGS__ )

#ifdef DEBUG_FLAG
#define MSG_DEBUG( ... ) printf( "debug: " __VA_ARGS__ )
#else
#define MSG_DEBUG( ... )
#endif

#define DEF_PORT 9000
#define SCGI_MSG_SZ 8192
#define SCGI_VAR_SZ 512

static int res, conn;
static uint16_t pos, len;
static char buf[SCGI_MSG_SZ] = { 0 };

int process() {
	//  0 - ok
	// -1 - execution errors
	// -2 - protocol errors
	# define GCI_ERROR( ... ) dprintf( conn, "Status: 500 Internal Error\nContent-Type: text/plain\n\n" __VA_ARGS__ )
	static uint16_t key, val;
	static char *script_filename;
	static FILE *cmd;
	
	static struct stat sb;
	
	res = read( conn, buf, SCGI_MSG_SZ );
	if ( res == SCGI_MSG_SZ ) {
		MSG_ERROR( "message size is out of range\n" );
		GCI_ERROR( "Message size is out of range\n" );
		return -2; // message too big
	}
	len = 0;
	for ( pos = 0; pos < res; pos++ ) {
		if ( '0' <= buf[pos] && buf[pos] <= '9' ) {
			len = ( len * 10 ) + ( buf[pos] - '0' );
			if ( len > SCGI_MSG_SZ ) {
				MSG_ERROR( "headers are too big\n" );
				GCI_ERROR( "Headers are too big\n" );
				return -2; // propagated headers too big
			}
		} else if ( buf[pos] == ':' ) {
			if ( len == 0 ) {
				MSG_ERROR( "message size is unknown\n" );
				GCI_ERROR( "Message size is unknown\n" );
				return -2;	// length in zero or not set
			} else {
				break;
			}
		} else {
			MSG_ERROR( "header length is unknown\n" );
			GCI_ERROR( "Message size is unknown\n" );
			return -2; // not found digits or colon at the begining 
		}
	}
	MSG_DEBUG( "request length: %d\n", len );
	
	key = ++pos;
	clearenv();
	for ( len += pos; pos < len; pos++ ) {
		if ( buf[pos] == 0 ) {
			val = ++pos;
			for ( ; pos < len; pos++ ) {
				if ( buf[pos] == 0 ) {
					MSG_DEBUG( "%s=%s\n", buf+key, buf+val );
					setenv( buf+key, buf+val, 1 );
					key = ++pos;
					break;
				}
			}
		}
	}
	setenv( "SCGI", "1", 1 );
	script_filename = getenv( "SCRIPT_FILENAME" );
	if ( script_filename == NULL ) {
		MSG_WARN( "primary script is unknown\n" );
		GCI_ERROR( "Primary script is unknown\n" );
		return -1;
	} else if ( stat( script_filename, &sb ) == 0 ) {
		if ( sb.st_mode & S_IXUSR ) {
			cmd = popen( script_filename, "r" );
			if ( cmd == NULL ) {
				MSG_ERROR( "child process terminated abnormally\n" );
				GCI_ERROR( "Child process terminated abnormally\n" );
				return -1;
			}
			#ifdef DEBUG_FLAG
			len = 0;
			#endif
			do {
				res = fread( buf, sizeof( char ), SCGI_MSG_SZ, cmd );
				if ( res > 0 ) {
					write( conn, buf, res );
				} else {
					#ifdef DEBUG_FLAG
					res = 0;
					#endif
					break;
				}
			}
			#ifdef DEBUG_FLAG
			while ( res == SCGI_MSG_SZ && ++len );
			MSG_DEBUG( "child process return %d bytes, exit code: %d\n", ( len * SCGI_MSG_SZ ) + res, pclose( cmd ) );
			#else
			while ( res == SCGI_MSG_SZ );
			pclose( cmd );
			#endif
		} else {
			MSG_ERROR( "file is not executable: %s\n", script_filename );
			GCI_ERROR( "File is not executable: %s\n", script_filename );
			return -1;
		}
	} else {
		MSG_WARN( "file is not found: %s\n", script_filename );
		GCI_ERROR( "File is not found: %s\n", script_filename );
		return -1;
	}
	
	return 0;
}


int main( int argc, char *argv[] ) {
	
	int sock, conn = AF_INET;
	struct sockaddr_in *inet_addr;
	struct sockaddr_un *unix_addr;
	void *sock_addr;
	socklen_t sx = 0;
	
	
	pos = 0;
	res = DEF_PORT;
	
	#define _ARG( S ) strcmp( argv[pos], S ) == 0
	while ( ++pos < argc ) {
		if ( _ARG( "-p" ) || _ARG( "--port" ) ) {
			if ( ++pos < argc ) {
				res = atoi( argv[pos] );
			}
		} else if ( _ARG( "-s" ) || _ARG( "--sock" ) ) {
			if ( ++pos < argc ) {
				conn = AF_UNIX;
				strcpy( buf, argv[pos] );
			}
		}
	}
	
	if ( conn == AF_UNIX ) {
		sx = sizeof( struct sockaddr_un );
		unix_addr = calloc( 1, sx );
		sock_addr = (void *)unix_addr;
		unix_addr->sun_family = AF_UNIX;
		strcpy( unix_addr->sun_path, buf );
		unlink( unix_addr->sun_path );
		MSG_DEBUG( "configured to listen socket %s\n", buf );
	} else {
		sx = sizeof( struct sockaddr_in );
		inet_addr = calloc( 1, sx );
		sock_addr = (void *)inet_addr;
		if ( res <= 0 ) {
			res = DEF_PORT;
		}
		inet_addr->sin_family = AF_INET;
		inet_addr->sin_addr.s_addr = htonl( INADDR_ANY );
		inet_addr->sin_port = htons( res );
		MSG_DEBUG( "configured to listen port %d\n", res );
	}
	
	sock = socket( conn, SOCK_STREAM, 0 );
	if ( sock == -1 ) {
		MSG_FATAL( "failed to create socket\n" );
		return res;
	}
	
	res = bind( sock, (struct sockaddr*)sock_addr, sx );
	if ( res == -1 ) {
		MSG_FATAL( "failed to bind socket\n" );
		return res;
	}
	if ( conn == AF_UNIX ) {
		chmod( unix_addr->sun_path, 0666 );
	}
	
	memset( sock_addr, 0, sx );
	listen( sock, 128 );
	clearenv();
	
	// MAIN LOOP
	while ( 1 ) {
		conn = accept( sock, (struct sockaddr*)sock_addr, &sx );
		if ( conn == -1 ) {
			MSG_FATAL( "failed to accept connection\n" );
			return -3;
		}
		
		process();
		
		shutdown( conn, SHUT_RDWR );
		res = close( conn );
		if ( res == -1 ) {
			MSG_FATAL( "failed to close connection\n" );
			return -3;
		}
	}
	
	shutdown( sock, SHUT_RDWR );
	read( sock, 0, SCGI_MSG_SZ );
	close( sock );

	return 0;
}


