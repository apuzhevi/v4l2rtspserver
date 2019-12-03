/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** ServerMediaSubsession.cpp
** 
** -------------------------------------------------------------------------*/

#include <sstream>
#include <linux/videodev2.h>

// project
#include "ServerMediaSubsession.h"
#include "MJPEGVideoSource.h"
#include "RSDeviceSource.h"

// ---------------------------------
//   BaseServerMediaSubsession
// ---------------------------------
FramedSource* BaseServerMediaSubsession::createSource(UsageEnvironment& env, FramedSource* videoES, const std::string& format) {
	return videoES;
}

RTPSink*  BaseServerMediaSubsession::createSink(
	UsageEnvironment& 	env, 
	Groupsock* 			rtpGroupsock, 
	unsigned char 		rtpPayloadTypeIfDynamic, 
	const std::string&	format, 
	RSDeviceSource*	source)
{
	RTPSink* videoSink = NULL;
	std::string sampling("YCbCr-4:2:2");

	videoSink = RawVideoRTPSink::createNew(env, rtpGroupsock, rtpPayloadTypeIfDynamic, source->getHeight(), source->getWidth(), 8, sampling.c_str());
	return videoSink;
}

char const* BaseServerMediaSubsession::getAuxLine(RSDeviceSource* source, RTPSink* rtpSink)
{
	const char* auxLine = NULL;
	if (rtpSink) {
		std::ostringstream os; 
		if (rtpSink->auxSDPLine()) {
			os << rtpSink->auxSDPLine();
		}
		else if (source) {
			unsigned char rtpPayloadType = rtpSink->rtpPayloadType();
			os << "a=fmtp:" << int(rtpPayloadType) << " " << source->getAuxLine() << "\r\n";				
			int width = source->getWidth();
			int height = source->getHeight();
			if ( (width > 0) && (height>0) ) {
				os << "a=x-dimensions:" << width << "," <<  height  << "\r\n";				
			}
		} 
		auxLine = strdup(os.str().c_str());
	}
	return auxLine;
}

