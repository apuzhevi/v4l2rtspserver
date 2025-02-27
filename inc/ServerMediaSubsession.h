/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** ServerMediaSubsession.h
** 
** -------------------------------------------------------------------------*/

#pragma once

#include <sys/stat.h>

#include <string>
#include <iomanip>
#include <iostream>
#include <fstream>

// live555
#include <liveMedia.hh>

// forward declaration
class RSDeviceSource;

// ---------------------------------
//   BaseServerMediaSubsession
// ---------------------------------
class BaseServerMediaSubsession
{
	public:
		BaseServerMediaSubsession(StreamReplicator* replicator): m_replicator(replicator) {};
	
	public:
		static FramedSource* createSource(UsageEnvironment& env, FramedSource * videoES, const std::string& format);
		static RTPSink* createSink(UsageEnvironment& env, Groupsock * rtpGroupsock, unsigned char rtpPayloadTypeIfDynamic, const std::string& format, RSDeviceSource* source);
		char const* getAuxLine(RSDeviceSource* source, RTPSink* rtpSink);
		
	protected:
		StreamReplicator* m_replicator;
};

