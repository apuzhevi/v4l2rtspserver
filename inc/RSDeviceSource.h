/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** V4l2DeviceSource.h
** 
** V4L2 live555 source 
**
** -------------------------------------------------------------------------*/


#ifndef RS_DEVICE_SOURCE
#define RS_DEVICE_SOURCE

#include <string>
#include <list> 
#include <iostream>
#include <iomanip>
#include <cassert>

#include <pthread.h>

// live555
#include <liveMedia.hh>

// Include RealSense Cross Platform API
#include <librealsense2/rs.hpp> 

using namespace rs2;

class RSDeviceSource: public FramedSource
{
	public:	
		// ---------------------------------
		// Captured frame
		// ---------------------------------
		struct Frame
		{
			Frame(char* buffer, int size, timeval timestamp) : m_buffer(buffer), m_size(size), m_timestamp(timestamp) {};
			Frame(const Frame&);
			Frame& operator=(const Frame&);
			~Frame()  { delete [] m_buffer; };
			
			char* m_buffer;
			unsigned int m_size;
			timeval m_timestamp;
		};
		
		// ---------------------------------
		// Compute simple stats
		// ---------------------------------
		class Stats
		{
			public:
				Stats(const std::string & msg) : m_fps(0), m_fps_sec(0), m_size(0), m_msg(msg) {};
				
			public:
				int notify(int tv_sec, int framesize);
			
			protected:
				int m_fps;
				int m_fps_sec;
				int m_size;
				const std::string m_msg;
		};
		
	public:
		static RSDeviceSource* createNew(UsageEnvironment& env, pipeline pipe, unsigned int queueSize);
		std::string getAuxLine() { return m_auxLine; };	
		void setAuxLine(const std::string auxLine) { m_auxLine = auxLine; };	
		int getWidth() { return m_width; };	
		int getHeight() { return m_height; };	
		int getBPP() { return m_bpp; };	

	protected:
		RSDeviceSource(UsageEnvironment& env, pipeline pipe, unsigned int queueSize);
		virtual ~RSDeviceSource();

	protected:	
		static void* threadStub(void* clientData) { return ((RSDeviceSource*) clientData)->thread();};
		void* thread();

		static void deliverFrameStub(void* clientData) {((RSDeviceSource*) clientData)->deliverFrame();};
		void deliverFrame();

		static void incomingPacketHandlerStub(void* clientData, int mask) { ((RSDeviceSource*) clientData)->incomingPacketHandler(); };
		void incomingPacketHandler();

		/// int getNextFrame(frameset fs);
		
		// overide FramedSource
		virtual void doGetNextFrame();	
		virtual void doStopGettingFrames();
					
	protected:
		std::list<Frame*> m_captureQueue;
		Stats m_in;
		Stats m_out;
		EventTriggerId m_eventTriggerId;
		pipeline m_pipe;
		unsigned int m_queueSize;
		pthread_t m_thid;
		pthread_mutex_t m_mutex;
		std::string m_auxLine;

	private:
		const int m_width	= 640;
		const int m_height	= 480;
		const int m_bpp		= 16;
		const int m_fps		= 30;
		int       m_fd;
};

#endif
