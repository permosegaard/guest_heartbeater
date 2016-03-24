#include <netdb.h> // getbyhostname
#include <stdio.h> // fclose
#include <locale.h> // setlocale
#include <stdlib.h> // system
#include <string.h> // memset
#include <unistd.h> // fork, chdir
#include <sys/socket.h> // AF_NET
#include <netinet/in.h> // sockaddr_in
#include <netinet/ip_icmp.h> // ICMP_ECHO

#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gprintf.h>
#include <gio/gio.h>

#include "vmGuestAppMonitorLib.h"

static gboolean CONFIG_DUMMY = 0;
static gboolean CONFIG_VERBOSE = 0;
static gboolean CONFIG_FOREGROUND = 0;
static gint CONFIG_SLEEP_SECONDS = 0;
static gint CONFIG_CONNECT_TIMEOUT_SECONDS = 0;
static gchar *CONFIG_TEST_SYSTEM = NULL;
static gchar *CONFIG_TEST_FOPEN = NULL;
static gchar *CONFIG_TEST_CONNECT = NULL;
static gchar *CONFIG_TEST_PING = NULL;

#define debug( ... ) __debug( __FILE__, __LINE__, __VA_ARGS__ )
void __debug( const gchar *file, int line, const gchar *format, ... ) {
	if ( CONFIG_VERBOSE ) {
		GTimeVal now;
		va_list arguments;

		g_get_current_time( &now );
		va_start( arguments, format );
		g_printf( "%s:%d %s - %s\n", file, line, g_time_val_to_iso8601( &now ), g_strdup_vprintf( format, arguments ) );
		va_end( arguments );
	}
}

gboolean test_fopen( const gchar *location ) { // if only statvfs worked reliably :(
	debug( "testing via fopen to '%s'", location );
	
	if ( g_file_test( location, G_FILE_TEST_EXISTS ) ) {
		debug( "file seems to exist, check location or remove and restart" );
		goto error;
	}
	
	FILE *file = g_fopen( location, "w" );
	if ( file ) {
		debug( "fopen tested passed" );
		fclose( file );
		g_unlink( location );
		return TRUE;
	}
	else {
		debug( "fopen test failed");
		debug( "check the running user has permissions and the filesystem is writeable" );
		goto error;
	}

error:	
	return FALSE;
}

gboolean test_system( const gchar *command ) {
	debug( "testing via system call the command '%s'", command );

	gint result = system( command );

	if ( result == 0 ) { 
		debug( "system test passed" );
		return TRUE;
	}
	else {
		debug( "system test failed" );
		debug( "command status code was '%d'", ( (gchar *) ( (long) result ) ) );
		goto error;
	}

error:	
	return FALSE;
}

gboolean test_connect( const gchar *destination ) {
	debug( "testing via connection to '%s'", destination );

	GSocketClient *client = g_socket_client_new();
	GSocketConnection *connection;

	g_socket_client_set_timeout( client, 1 );

	connection = g_socket_client_connect_to_host( client, destination, 0, NULL, NULL );
	g_object_unref( client );

	if (connection != NULL) {
		debug( "connect test passed" );
		g_object_unref( connection );
		return TRUE;
	}
	else { debug( "connect test failed" ); goto error; }

error:
	return FALSE;
}

// https://stackoverflow.com/questions/4657406/ping-function-fails-after-1020-tries
// http://sotodayithought.blogspot.co.uk/2010/03/simple-ping-implementation-in-c.html
gboolean test_ping( const gchar *destination ) { // sadly gio doesn't do raw sockets well :(
	debug( "testing via ping to '%s'", destination );

	int sock;
	struct sockaddr_in address;
	struct hostent *host;
	struct icmp *packet;
	char packet_contents[ 192 ];
	int checksum = 0;
	int checksum_length = 192;
	int checksum_buffer;

	if ( ( sock = socket( AF_INET, SOCK_RAW, 1 ) ) < 0 ) {
		debug( "unable to create socket" );
		debug( "root privileges required, check and try again" );
		goto error;
	}
	
	memset( &address, 0, sizeof( struct sockaddr_in ) );
	address.sin_family = AF_INET;
		
	if ( ! ( host = gethostbyname( destination ) ) ) {
		debug( "cannot determine ip from hostname" );
		goto error;
	}

	memcpy( &address.sin_addr, host->h_addr_list[ 0 ], sizeof( address.sin_addr ) );
	packet = ( struct icmp * ) packet_contents;
	memset( packet, 0, sizeof( packet_contents ) );
	packet->icmp_type = ICMP_ECHO;
	packet->icmp_id = 0;
	packet->icmp_seq = 0;
	packet->icmp_code = 0;
	packet->icmp_cksum = 0;

	if ( sendto( sock, packet_contents, sizeof( packet_contents ), 0, ( struct sockaddr * ) &address, sizeof( struct sockaddr_in ) != sizeof( packet_contents ) ) ) {
		debug( "sending packet not finished" );
		goto error;
	}

	g_usleep( 0.2 * G_USEC_PER_SEC );

	if ( recvfrom( sock, packet_contents, sizeof( packet_contents ), 0, NULL, NULL ) < 0 ) {
		debug( "no response received" );
		goto error;
	}

	if ( ( ( struct icmp * ) packet )->icmp_type == ICMP_ECHOREPLY ) { return TRUE; }
	else {
		debug( "response was not echoreply type" );
		goto error;
	}

error:
	return FALSE;
}

void mainloop() {
	while( TRUE ) {
		gboolean vmware_heartbeat_send = FALSE;

		if ( CONFIG_TEST_SYSTEM != NULL ) {
			if ( ! test_system( CONFIG_TEST_SYSTEM ) ) { break; }
		}

		if ( CONFIG_TEST_FOPEN != NULL ) {
			if ( ! test_fopen( CONFIG_TEST_FOPEN ) ) { break; }
		}
		
		if ( CONFIG_TEST_CONNECT != NULL ) {
			if ( ! test_connect( CONFIG_TEST_CONNECT ) ) { break; }
		}
		
		if ( CONFIG_TEST_PING != NULL ) {
			if ( ! test_ping( CONFIG_TEST_PING ) ) { break; }
		}
		
		if ( CONFIG_DUMMY || VMGuestAppMonitor_MarkActive() == VMGUESTAPPMONITORLIB_ERROR_SUCCESS ) { vmware_heartbeat_send = TRUE; }
		
		if ( ! vmware_heartbeat_send ) { debug( "unable to send heartbeat" ); break; }
		else { debug( "heartbeat sent sucessfully" ); }

		debug( g_strdup_printf( "sleeping %d seconds", CONFIG_SLEEP_SECONDS ) );
		g_usleep( CONFIG_SLEEP_SECONDS * G_USEC_PER_SEC );
	}
}

int main( int argc, char *argv[] ) { // TODO: handle sigint/etc. for clean shutdown? needs thought.
	GError *error = NULL;
	GOptionContext *context;
	gboolean vmware_monitor_enable = FALSE;

	static GOptionEntry config_entries[] = {
		{ "dummy", 'd', 0, G_OPTION_ARG_NONE, &CONFIG_DUMMY, "do not send anything to vmware", NULL },
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &CONFIG_VERBOSE, "print extra debug info", NULL },
		{ "foreground", 'f', 0, G_OPTION_ARG_NONE, &CONFIG_FOREGROUND, "do not fork into background", NULL },
		{ "seconds", 's', 0, G_OPTION_ARG_INT, &CONFIG_SLEEP_SECONDS, "sleep between tests", "[seconds]" },
		{ "connect-timeout", 's', 0, G_OPTION_ARG_INT, &CONFIG_CONNECT_TIMEOUT_SECONDS, "timeout for connect to complete", "[seconds]" },
		{ "test-system", 0, 0, G_OPTION_ARG_STRING, &CONFIG_TEST_SYSTEM, "test using a command passed to the system call", "[command passed to sh -c]" },
		{ "test-fopen", 0, 0, G_OPTION_ARG_STRING, &CONFIG_TEST_FOPEN, "test using fopen in 'w' mode", "[path to test]" },
		{ "test-connect", 0, 0, G_OPTION_ARG_STRING, &CONFIG_TEST_CONNECT, "test using tcp connect", "[host:port]" },
		{ "test-ping", 0, 0, G_OPTION_ARG_STRING, &CONFIG_TEST_PING, "test using icmp ping", "[host]" }
	};

	context = g_option_context_new( "" );
	g_option_context_add_main_entries( context, config_entries, NULL );
	if ( ! g_option_context_parse( context, &argc, &argv, &error ) ) { g_printf( "option parsing failed: %s\n", error->message ); }
	else {
		// defaults here
		if ( CONFIG_DUMMY == 0 ) { CONFIG_DUMMY = FALSE; }
		if ( CONFIG_VERBOSE == 0 ) { CONFIG_VERBOSE = FALSE; }
		if ( CONFIG_FOREGROUND == 0 ) { CONFIG_FOREGROUND = FALSE; }
		if ( CONFIG_SLEEP_SECONDS == 0 ) { CONFIG_SLEEP_SECONDS = 10; }
		if ( CONFIG_CONNECT_TIMEOUT_SECONDS == 0 ) { CONFIG_CONNECT_TIMEOUT_SECONDS = 1; }

		if ( CONFIG_DUMMY || VMGuestAppMonitor_Enable() == VMGUESTAPPMONITORLIB_ERROR_SUCCESS ) { vmware_monitor_enable = TRUE; }

		if ( ! vmware_monitor_enable ) { debug( "unable to enable guest app monitoring" ); }
		else {
			if ( ! CONFIG_FOREGROUND ) {
				CONFIG_VERBOSE = FALSE; // cannot log to stdout with it closed

				pid_t pid;
				for ( int i = 0; i < 1; i++ ) { // who'd want to fork once anyway?
					pid = fork();
					if ( pid < 0 ) { return 1; }
					else if ( pid > 0 ) { return 0; }
				}

				umask( 0 );
				chdir( "/" );

				fclose( stdout );
				fclose( stderr );
			}
			debug( "guest application monitoring enabled" );
			mainloop();
		}
	}

	return 1;
}
