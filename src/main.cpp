/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** main.cpp
** 
** V4L2 RTSP streamer                                                                 
**                                                                                    
** H264 capture using V4L2                                                            
** RTSP using live555                                                                 
**                                                                                    
** -------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <fcntl.h>

#include <sstream>

// libv4l2
#include <linux/videodev2.h>

// live555
#include <BasicUsageEnvironment.hh>
#include <GroupsockHelper.hh>

// project
#include "logger.h"

#include "RSDeviceSource.h"
#include "ServerMediaSubsession.h"
#include "UnicastServerMediaSubsession.h"
#include "HTTPServer.h"

// Include RealSense Cross Platform API
#include <librealsense2/rs.hpp> 

// -----------------------------------------
//    signal handler
// -----------------------------------------
char quit = 0;
void sighandler(int n)
{ 
	printf("SIGINT\n");
	quit =1;
}


// -----------------------------------------
//    create UserAuthenticationDatabase for RTSP server
// -----------------------------------------
UserAuthenticationDatabase* createUserAuthenticationDatabase(const std::list<std::string> & userPasswordList, const char* realm)
{
	UserAuthenticationDatabase* auth = NULL;
	if (userPasswordList.size() > 0)
	{
		auth = new UserAuthenticationDatabase(realm, (realm != NULL) );
		
		std::list<std::string>::const_iterator it;
		for (it = userPasswordList.begin(); it != userPasswordList.end(); ++it)
		{
			std::istringstream is(*it);
			std::string user;
			getline(is, user, ':');	
			std::string password;
			getline(is, password);	
			auth->addUserRecord(user.c_str(), password.c_str());
		}
	}
	
	return auth;
}

// -----------------------------------------
//    create RTSP server
// -----------------------------------------
RTSPServer* createRTSPServer(
	UsageEnvironment&				env, 
	unsigned short 					rtspPort, 
	unsigned short 					rtspOverHTTPPort, 
	int 							timeout, 
	unsigned int 					hlsSegment, 
	const std::list<std::string> & 	userPasswordList, 
	const char* 					realm, 
	const std::string & 			webroot) 
{
	UserAuthenticationDatabase* auth = createUserAuthenticationDatabase(userPasswordList, realm);
	RTSPServer* rtspServer = HTTPServer::createNew(env, rtspPort, auth, timeout, hlsSegment, webroot);
	if (rtspServer != NULL) {
		// set http tunneling
		if (rtspOverHTTPPort) {
			rtspServer->setUpTunnelingOverHTTP(rtspOverHTTPPort);
		}
	}
	return rtspServer;
}

/*
// -----------------------------------------
//    create FramedSource server
// -----------------------------------------
FramedSource* createFramedSource(UsageEnvironment* env, int format, RSDeviceInterface* videoCapture, int outfd, int queueSize, bool useThread, bool repeatConfig)
{
	return RSDeviceSource::createNew(*env, videoCapture, outfd, queueSize, useThread);
}
*/

// -----------------------------------------
//    add an RTSP session
// -----------------------------------------
int addSession(RTSPServer* rtspServer, const std::string & sessionName, const std::list<ServerMediaSubsession*> & subSession)
{
	int nbSubsession = 0;
	if (subSession.empty() == false)
	{
		UsageEnvironment& env(rtspServer->envir());
		ServerMediaSession* sms = ServerMediaSession::createNew(env, sessionName.c_str());
		if (sms != NULL)
		{
			std::list<ServerMediaSubsession*>::const_iterator subIt;
			for (subIt = subSession.begin(); subIt != subSession.end(); ++subIt)
			{
				sms->addSubsession(*subIt);
				nbSubsession++;
			}
			
			rtspServer->addServerMediaSession(sms);

			char* url = rtspServer->rtspURL(sms);
			if (url != NULL)
			{
				LOG(NOTICE) << "Play this stream using the URL \"" << url << "\"" << std::endl;;
				delete[] url;			
			}
		}
	} else {
		LOG(NOTICE) << "Subsession is already exist" << std::endl;;
	}
	return nbSubsession;
}

// -----------------------------------------
//    convert string video format to fourcc 
// -----------------------------------------
int decodeVideoFormat(const char* fmt)
{
	char fourcc[4];
	memset(&fourcc, 0, sizeof(fourcc));
	if (fmt != NULL)
	{
		strncpy(fourcc, fmt, 4);	
	}
	return v4l2_fourcc(fourcc[0], fourcc[1], fourcc[2], fourcc[3]);
}


// -----------------------------------------
//    params
// -----------------------------------------

struct params {
	unsigned short rtspPort;
	unsigned short rtspOverHTTPPort;

	std::string url;

	int width;
	int height;
	int queueSize;
	int fps;

	int timeout;
	int defaultHlsSegment;

	int verbose;
	std::string outputFile;
	std::string webroot;

	bool multicast;
	std::string maddr;
	bool repeatConfig;

	int openflags; 
	bool useThread;
	unsigned int format;

	unsigned int hlsSegment;
	const char* realm;

	std::list<std::string> userPasswordList;
	std::list<std::string> devList;
	std::list<unsigned int> videoformatList;

} gParams = {
	8554,
	0,

	"unicast",

	0,
	0,
	10,
	25,

	65,
	2,

	0,
	"",
	"",

	false,
	"",
	true,

	O_RDWR | O_NONBLOCK,
	true,
	0,

	0,
	NULL
};

// -----------------------------------------
//    usage
// -----------------------------------------

void usage(std::string name) {
	std::cout << name << " [-v[v]] [-Q queueSize] [-O file]"                                        << std::endl;
	std::cout << "\t          [-I interface] [-P RTSP port] [-p RTSP/HTTP port] [-m multicast url] [-u unicast url] [-M multicast addr] [-c] [-t timeout] [-T] [-S[duration]]" << std::endl;
	std::cout << "\t          [-r] [-w] [-s] [-f[format] [-W width] [-H height] [-F fps] [device] [device]"                        << std::endl;
	std::cout << "\t -v               : verbose"                                                                                          << std::endl;
	std::cout << "\t -vv              : very verbose"                                                                                     << std::endl;
	std::cout << "\t -Q <length>      : Number of frame queue  (default " << gParams.queueSize << ")"                                              << std::endl;
	std::cout << "\t -O <output>      : Copy captured frame to a file or a V4L2 device"                                                   << std::endl;
	std::cout << "\t -b <webroot>     : path to webroot" << std::endl;
	
	std::cout << "\t RTSP/RTP options"                                                                                           << std::endl;
	std::cout << "\t -I <addr>        : RTSP interface (default autodetect)"                                                              << std::endl;
	std::cout << "\t -P <port>        : RTSP port (default "<< gParams.rtspPort << ")"                                                            << std::endl;
	std::cout << "\t -p <port>        : RTSP over HTTP port (default "<< gParams.rtspOverHTTPPort << ")"                                          << std::endl;
	std::cout << "\t -U <user>:<pass> : RTSP user and password"                                                                    << std::endl;
	std::cout << "\t -R <realm>       : use md5 password 'md5(<username>:<realm>:<password>')"                                            << std::endl;
	std::cout << "\t -u <url>         : unicast url (default " << gParams.url << ")"                                                              << std::endl;
	std::cout << "\t -c               : don't repeat config (default repeat config before IDR frame)"                                     << std::endl;
	std::cout << "\t -t <timeout>     : RTCP expiration timeout in seconds (default " << gParams.timeout << ")"                                   << std::endl;
	
	std::cout << "\t V4L2 options"                                                                                               << std::endl;
	std::cout << "\t -r               : V4L2 capture using read interface (default use memory mapped buffers)"                            << std::endl;
	std::cout << "\t -w               : V4L2 capture using write interface (default use memory mapped buffers)"                           << std::endl;
	std::cout << "\t -B               : V4L2 capture using blocking mode (default use non-blocking mode)"                                 << std::endl;
	std::cout << "\t -s               : V4L2 capture using live555 mainloop (default use a reader thread)"                                << std::endl;
	std::cout << "\t -f               : V4L2 capture using current capture format (-W,-H,-F are ignored)"                                 << std::endl;
	std::cout << "\t -f<format>       : V4L2 capture using format (-W,-H,-F are used)"                                                    << std::endl;
	std::cout << "\t -W <width>       : V4L2 capture width (default "<< gParams.width << ")"                                                      << std::endl;
	std::cout << "\t -H <height>      : V4L2 capture height (default "<< gParams.height << ")"                                                    << std::endl;
	std::cout << "\t -F <fps>         : V4L2 capture framerate (default "<< gParams.fps << ")"                                                    << std::endl;
	std::cout << "\t -G <w>x<h>[x<f>] : V4L2 capture format (default "<< gParams.width << "x" << gParams.height << "x" << gParams.fps << ")"  << std::endl;
	
	exit(0);
}

void decode_parameters(int argc, char** argv) {
	// decode parameters
	int c = 0;     
	while ((c = getopt (argc, argv, "v::Q:O:b:" "I:P:p:m:u:M:ct:S::" "R:U:" "rwBsf::F:W:H:G:" "A:C:a:" "Vh")) != -1) {
		switch (c) {
		case 'v':	gParams.verbose    = 1; if (optarg && *optarg=='v') gParams.verbose++;  break;
		case 'Q':	gParams.queueSize  = atoi(optarg); break;
		case 'O':	gParams.outputFile = optarg; break;
		case 'b':	gParams.webroot = optarg; break;
		
		// RTSP/RTP
		case 'I':       ReceivingInterfaceAddr  = inet_addr(optarg); break;
		case 'P':	gParams.rtspPort                = atoi(optarg); break;
		case 'p':	gParams.rtspOverHTTPPort        = atoi(optarg); break;
		case 'u':	gParams.url                     = optarg; break;
		case 'c':	gParams.repeatConfig            = false; break;
		case 't':	gParams.timeout                 = atoi(optarg); break;
		
		// users
		case 'R':   gParams.realm                   = optarg; break;
		case 'U':   gParams.userPasswordList.push_back(optarg); break;
		
		// V4L2
		case 'B':	gParams.openflags = O_RDWR; break;	
		case 's':	gParams.useThread =  false; break;
		case 'f':	gParams.format    = 0; /* decodeVideoFormat(optarg); if (gParams.format) { gParams.videoformatList.push_back(gParams.format); }; */  break;
		case 'F':	gParams.fps       = atoi(optarg); break;
		case 'W':	gParams.width     = atoi(optarg); break;
		case 'H':	gParams.height    = atoi(optarg); break;
		case 'G':   sscanf(optarg,"%dx%dx%d", &gParams.width, &gParams.height, &gParams.fps); break;
					
		// version
		case 'V':	
			std::cout << VERSION << std::endl;
			exit(0);			
		break;
		
		// help
		case 'h':
		default:	usage(argv[0]);
		}
	}

	while (optind < argc) {
		gParams.devList.push_back(argv[optind]);
		optind++;
	}
}

// -----------------------------------------
//    entry point
// -----------------------------------------
int main(int argc, char** argv) {
	// default parameters
	const char* defaultPort = getenv("PORT");
	if (defaultPort != NULL) {
		gParams.rtspPort = atoi(defaultPort);
	}

	decode_parameters(argc, argv);
	
	// init logger
	initLogger(gParams.verbose);
     
	// create live555 environment
	TaskScheduler* scheduler = BasicTaskScheduler::createNew();
	UsageEnvironment* env = BasicUsageEnvironment::createNew(*scheduler);	

	// create RTSP server
	OutPacketBuffer::maxSize = 1025 * 1024;
	RTSPServer* rtspServer = createRTSPServer(*env, gParams.rtspPort, gParams.rtspOverHTTPPort, gParams.timeout, 
												gParams.hlsSegment, gParams.userPasswordList, gParams.realm, gParams.webroot);
	if (rtspServer == NULL) {
		LOG(ERROR) << "Failed to create RTSP server: " << env->getResultMsg() << std::endl;
	} else {			
		StreamReplicator* videoReplicator = NULL;
		StreamReplicator* videoReplicator_2 = NULL;

		std::string rtpFormat("video/RAW");

		LOG(NOTICE) << "Create RS pipeline..." << std::endl;
		pipeline pipe;
		config cfg;
		cfg.enable_stream(rs2_stream::RS2_STREAM_DEPTH, 640, 480, RS2_FORMAT_Z16, 30); // AP: hardcode all constants, they never chage
		cfg.enable_stream(rs2_stream::RS2_STREAM_COLOR, 424, 240, RS2_FORMAT_RGB8, 30); // AP: hardcode all constants, they never chage
		//nhershko
		pipe.start(cfg);

		LOG(NOTICE) << "Create Source ..." << std::endl;
		FramedSource* videoSource = RSDeviceSource::createNew(*env, pipe, 42); // AP: 42 can replace any integer value
		FramedSource* videoSource2 = RSDeviceSource::createNew(*env, pipe, 41); // AP: 42 can replace any integer value
		
		if (videoSource == NULL) {
			LOG(FATAL) << "Unable to create source for device " << std::endl;
		} else {
			videoReplicator = StreamReplicator::createNew(*env, videoSource, false);
			videoReplicator_2 = StreamReplicator::createNew(*env, videoSource2, false);
		}

		// Create Unicast Session					
		std::list<ServerMediaSubsession*> subSession;
		std::list<ServerMediaSubsession*> subSession2;
		if (videoReplicator) {
			subSession.push_back(UnicastServerMediaSubsession::createNew(*env, videoReplicator, rtpFormat));				
			subSession.push_back(UnicastServerMediaSubsession::createNew(*env, videoReplicator_2, rtpFormat));				
		}

		if (addSession(rtspServer, gParams.url, subSession)) {
			//addSession(rtspServer, gParams.url+ "2", subSession2);
			// main loop
			signal(SIGINT,sighandler);
			env->taskScheduler().doEventLoop(&quit); 
			LOG(NOTICE) << "Exiting..." << std::endl;			
		}
		
		Medium::close(rtspServer);
	}
	
	env->reclaim();
	delete scheduler;	
	
	return 0;
}



