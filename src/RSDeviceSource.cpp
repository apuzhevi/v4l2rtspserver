/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** v4l2DeviceSource.cpp
** 
** V4L2 Live555 source 
**
** -------------------------------------------------------------------------*/

#include <fcntl.h>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <cmath>
#include <cerrno>
#include <cstring>
#include <clocale>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

// project
#include "logger.h"
#include "RSDeviceSource.h"

// ---------------------------------
// RealSense FramedSource Stats
// ---------------------------------
int  RSDeviceSource::Stats::notify(int tv_sec, int framesize)
{
	m_fps++;
	m_size += framesize;
	if (tv_sec != m_fps_sec)
	{
		LOG(INFO) << m_msg  << "tv_sec:" <<   tv_sec << " fps:" << m_fps << " bandwidth:"<< (m_size/128) << "kbps" << std::endl;		
		m_fps_sec = tv_sec;
		m_fps = 0;
		m_size = 0;
	}
	return m_fps;
}

// ---------------------------------
// RealSense FramedSource
// ---------------------------------
RSDeviceSource* RSDeviceSource::createNew(UsageEnvironment& env, pipeline pipe, unsigned int queueSize) 
{ 	
	return (new RSDeviceSource(env, pipe, queueSize));
}

// Constructor
RSDeviceSource::RSDeviceSource(UsageEnvironment& env, pipeline pipe, unsigned int queueSize) 
	: FramedSource(env), 
	m_in("in"), 
	m_out("out") , 
	m_pipe(pipe),
	m_queueSize(queueSize)
{
	m_eventTriggerId = envir().taskScheduler().createEventTrigger(RSDeviceSource::deliverFrameStub);
	memset(&m_thid, 0, sizeof(m_thid));
	memset(&m_mutex, 0, sizeof(m_mutex));

	pthread_mutex_init(&m_mutex, NULL);
	pthread_create(&m_thid, NULL, threadStub, this);	

	m_fd = open("/tmp/stream.raw", O_WRONLY | O_CREAT);
	if (m_fd == -1) {
		LOG(NOTICE) << "cannot create dump: " << std::strerror(errno) << std::endl;
	}

}

// Destructor
RSDeviceSource::~RSDeviceSource()
{	
	envir().taskScheduler().deleteEventTrigger(m_eventTriggerId);
	pthread_join(m_thid, NULL);	
	pthread_mutex_destroy(&m_mutex);

	if (m_fd > 0) {
		::close(m_fd);
	}
}

// thread mainloop
void* RSDeviceSource::thread()
{
	int stop = 0;
	timeval tv;
	
	LOG(NOTICE) << "begin thread" << std::endl; 
	while (!stop) {
		unsigned int frameSize = getWidth() * getHeight() * (getBPP() / 8);

		// Wait for next set of frames from the camera
		frameset fs = m_pipe.wait_for_frames(); 

		gettimeofday(&tv, NULL);												
		m_in.notify(tv.tv_sec, frameSize);
		char* buf = new char[frameSize];
		const void * frameBuf;
		if (m_queueSize==42)
		{
			frameBuf = fs.get_depth_frame().get_data();
			LOG(DEBUG) << "depth frame size:bpp:width is: " << fs.get_depth_frame().get_data_size() << ":" << fs.get_depth_frame().get_bytes_per_pixel() << ":" << fs.get_depth_frame().get_width() << std::endl;
		}
		else
		{
			frameBuf = fs.get_color_frame().get_data();
			LOG(DEBUG) << "color frame size:bpp:width is: " << fs.get_color_frame().get_data_size() << ":" << fs.get_color_frame().get_bytes_per_pixel() << ":" << fs.get_color_frame().get_width() << std::endl;
		}
		
		if (frameBuf) {
			LOG(DEBUG) << "frame arrived\ttimestamp:" << tv.tv_sec << "." << tv.tv_usec << "\tsize:" << frameSize << std::endl;
			memcpy(buf, frameBuf, frameSize);
		} else {
			LOG(DEBUG) << "frame arrived\ttimestamp:" << tv.tv_sec << "." << tv.tv_usec << "\tN/A" << std::endl;
		}

		pthread_mutex_lock (&m_mutex);
		while (m_captureQueue.size() >= m_queueSize) {
			LOG(DEBUG) << "Queue full size drop frame size:"  << (int)m_captureQueue.size() << std::endl;
			delete m_captureQueue.front();
			m_captureQueue.pop_front();
		}
		m_captureQueue.push_back(new Frame(buf, frameSize, tv));	
		pthread_mutex_unlock (&m_mutex);
		
		// post an event to ask to deliver the frame 
		// AP: why do we need it if the sink is asking by itself?
		envir().taskScheduler().triggerEvent(m_eventTriggerId, this);
	}
	LOG(NOTICE) << "end thread" << std::endl; 
	return NULL;
}

// getting FrameSource callback
void RSDeviceSource::doGetNextFrame()
{
	LOG(DEBUG) << "RSDeviceSource::doGetNextFrame" << std::endl;	
	deliverFrame();
}

// stopping FrameSource callback
void RSDeviceSource::doStopGettingFrames()
{
	LOG(DEBUG) << "RSDeviceSource::doStopGettingFrames" << std::endl;	
	FramedSource::doStopGettingFrames();
}

// deliver frame to the sink
void RSDeviceSource::deliverFrame()
{			
	LOG(DEBUG) << "RSDeviceSource::deliverFrame -> " << std::endl;	
	if (isCurrentlyAwaitingData()) {
		LOG(DEBUG) << "sink is asking" << std::endl;	
		fDurationInMicroseconds = 0;
		fFrameSize = 0;
		
		pthread_mutex_lock (&m_mutex);
		if (m_captureQueue.empty()) {
			LOG(DEBUG) << "Queue is empty" << std::endl;		
		} else {				
			timeval curTime;
			gettimeofday(&curTime, NULL);			
			Frame * frame = m_captureQueue.front();
			m_captureQueue.pop_front();
	
			m_out.notify(curTime.tv_sec, frame->m_size);
			if (frame->m_size > fMaxSize) {
				fFrameSize = fMaxSize;
				fNumTruncatedBytes = frame->m_size - fMaxSize;
			} else {
				fFrameSize = frame->m_size;
			}
			timeval diff;
			timersub(&curTime, &(frame->m_timestamp),&diff);

			LOG(DEBUG) << "deliverFrame\ttimestamp:" << curTime.tv_sec  << "." << curTime.tv_usec << 
			                          "\tsize:" << fFrameSize <<
									  "\tdiff:" <<  (diff.tv_sec*1000+diff.tv_usec/1000) << "ms" << 
									  "\tqueue:" << m_captureQueue.size() <<
									  "\tm_size: " << frame->m_size <<
									  "\tfMaxSize: " << fMaxSize <<
									  "\tfFrameSize: " << fFrameSize << std::endl;
			
			fPresentationTime = frame->m_timestamp;
			memcpy(fTo, frame->m_buffer, fFrameSize);
			delete frame;

			//write(m_fd, fTo, fFrameSize);
		}
		pthread_mutex_unlock (&m_mutex);
		
		if (fFrameSize > 0)	{
			// send Frame to the consumer
			FramedSource::afterGetting(this);			
		}
	} else {
		LOG(DEBUG) << "sink wasn't asking" << std::endl;	
	}
}
	
// FrameSource callback on read event
void RSDeviceSource::incomingPacketHandler()
{
	LOG(NOTICE) << "incomingPacketHandler" << std::endl;
}
