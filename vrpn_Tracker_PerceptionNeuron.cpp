#include "quat.h"
#include "vrpn_Tracker_PerceptionNeuron.h"

#define SCALE_AXIS (0.01)

#include <thread>
#include <mutex>
#include <chrono>
#define MAIN_LOOP_THREAD_TIMEOUT 16
const int MAX_SENSORS_NEURON = 122*6; // adding extra space for reference. assuming worst case, when both displacement and rotation comes. 
const int MAX_NUM_SENSORS_PER_SUIT = 59;

#ifdef VRPN_INCLUDE_PERCEPTION_NEURON
//#define DEBUG

void __stdcall bvhFrameDataReceived(void* customedObj, SOCKET_REF sender, BvhDataHeader* header, float* data);
void __stdcall CalcFrameDataReceive(void* customedObj, SOCKET_REF sender, CalcDataHeader* header, float* data);

class PerceptionNeuronHandler
{
public:
	static PerceptionNeuronHandler* getInstance()
	{
		if (singleton == NULL)
		{
			singleton = new PerceptionNeuronHandler();
			singleton->startThread();
		}
		return singleton;
	}
	bool enable(vrpn_Tracker_PerceptionNeuron* h, char* ipNeuron, int tcpPort, int udpPort)
	{
		bool resp = false;

		if (handler != NULL)
		{
			if (handler == h)
			{
				fprintf(stderr, "Perception Neuron: Registering the same handler at least twice!\n");
			}
			else
			{
				fprintf(stderr, "Perception Neuron: Registering more than one handler!\n");
			}
		}
		handler = h;
		fprintf(stderr, "PerceptionNeuron tcpPort:[%d] udpPort:[%d] \n", tcpPort, udpPort);
		fflush(stderr);
		if (tcpPort != -1 && sockTCPRef == NULL)
		{
			sockTCPRef = BRConnectTo(ipNeuron, tcpPort);
			if (sockTCPRef)
			{
				resp = true;
			}
		}
		else if (udpPort != -1 && sockUDPRef == NULL)
		{
			sockUDPRef = BRStartUDPServiceAt(udpPort);
			if (sockUDPRef)
			{
				resp = true;
			}
			else
			{
				fprintf(stderr, "UDP error:[%s] \n", BRGetLastErrorMessage());
			}
		}
		return resp;
	}
	bool disable()
	{
		bool resp = false;

		handler = NULL;
		if (sockTCPRef)
		{
			BRCloseSocket(sockTCPRef);
			sockTCPRef = NULL;
			resp = true;
		}
		else if (sockUDPRef)
		{
			BRCloseSocket(sockUDPRef);
			sockUDPRef = NULL;
			resp = true;
		}
		return resp;
	}
	void handleData(SOCKET_REF sender, BvhDataHeader* header, float* data)
	{
		if (handler != NULL && BRGetSocketStatus(sender) == CS_Running)
		{
			int numberOfFloats = getNumberOfFloats(header);
			// Securely copy the information and leave the other thread to read it
			std::lock_guard<std::mutex> lock(mtx);
			memcpy(&myHeader, header, sizeof(BvhDataHeader));
			memcpy(myData, data, numberOfFloats * sizeof(float));
			itsBeingProcessed = false;
			//fprintf(stderr, "[Thread %lu] handleData in bvhFrameDataReceived\n", (unsigned long)std::hash<std::thread::id>()(std::this_thread::get_id()));
		}
	}
	~PerceptionNeuronHandler()
	{
		terminateThread = true;
		delete[] myData;
		implThread->join();
	}
	void processData()
	{
		std::lock_guard<std::mutex> lock(mtx);
		if (!itsBeingProcessed)
		{
			handler->handleData(&myHeader, myData);
			itsBeingProcessed = true;
			//fprintf(stderr, "[Thread %lu] processData\n", (unsigned long)std::hash<std::thread::id>()(std::this_thread::get_id()));
		}
	}
private:
	static PerceptionNeuronHandler* singleton;

	vrpn_Tracker_PerceptionNeuron* handler;
	SOCKET_REF sockTCPRef;
	SOCKET_REF sockUDPRef;
	std::thread* implThread;
	bool terminateThread;
	
	// shared data and mutex
	std::mutex mtx;
	bool itsBeingProcessed;
	BvhDataHeader myHeader;
	float* myData;

	PerceptionNeuronHandler()
	{
		handler = NULL;
		sockTCPRef = NULL;
		sockUDPRef = NULL;
		implThread = NULL;
		itsBeingProcessed = true;
		terminateThread = false;
		myData = new float[MAX_SENSORS_NEURON * 6];
	};
	static void doThisInThread()
	{
		BRRegisterFrameDataCallback(singleton, bvhFrameDataReceived);
		BRRegisterCalculationDataCallback(singleton, CalcFrameDataReceive);

		while (!singleton->terminateThread)
		{
			// making time for the Neuron's system to call the callbacks
			std::this_thread::sleep_for(std::chrono::milliseconds(MAIN_LOOP_THREAD_TIMEOUT));
		}
	}
	void startThread()
	{
		implThread = new std::thread(doThisInThread);
	}
	int getNumberOfFloats(BvhDataHeader* header)
	{
		int dataIndex = 0;
		int nr = MAX_NUM_SENSORS_PER_SUIT;

		int curSel = nr - 1;
		if (header->WithDisp)
		{
			dataIndex = curSel * 6;
			if (header->WithReference)
			{
				dataIndex += 6;
			}

			// plus final data
			dataIndex += 6;
		}
		else // ! (header->WithDisp)
		{
			dataIndex = 3 + curSel * 3;
			if (header->WithReference)
			{
				dataIndex += 6;
			}

			// plus final data
			dataIndex += 3;

		}
		return dataIndex;
	}
};

PerceptionNeuronHandler* PerceptionNeuronHandler::singleton = NULL;

void __stdcall bvhFrameDataReceived(void* customedObj, SOCKET_REF sender, BvhDataHeader* header, float* data)
{
#ifdef DEBUG
	printf("%s\n", __PRETTY_FUNCTION__);
#endif
	PerceptionNeuronHandler* obj = static_cast<PerceptionNeuronHandler*>(customedObj);
	obj->handleData(sender, header, data);
}

void __stdcall CalcFrameDataReceive(void* customedObj, SOCKET_REF sender, CalcDataHeader* header, float* data)
{
#ifdef DEBUG
	printf("%s\n", __PRETTY_FUNCTION__);
#endif
	printf("CalcFrameDataReceive\n");
}

int vrpn_Tracker_PerceptionNeuron::handle_first_connection_message(void *userdata, vrpn_HANDLERPARAM)
{
#ifdef DEBUG
	printf("%s\n", __PRETTY_FUNCTION__);
#endif
	// Do nothing for the moment...
	return 0;
}

int vrpn_Tracker_PerceptionNeuron::handle_got_connection_message(void *userdata, vrpn_HANDLERPARAM)
{
#ifdef DEBUG
	printf("%s\n", __PRETTY_FUNCTION__);
#endif
	// Do nothing for the moment...
	return 0;
}

int vrpn_Tracker_PerceptionNeuron::handle_connection_dropped_message(void *userdata, vrpn_HANDLERPARAM)
{
#ifdef DEBUG
	printf("%s\n", __PRETTY_FUNCTION__);
#endif
	/*
	vrpn_Tracker_PerceptionNeuron *me = static_cast<vrpn_Tracker_PerceptionNeuron *>(userdata);
	// it still crashes sometimes... I guess more exceptions have to be handled, plus something related to the UDP connection...
	delete me;
	*/
	// Do nothing for the moment...
	return 0;
}

int vrpn_Tracker_PerceptionNeuron::handle_dropped_last_connection_message(void *userdata, vrpn_HANDLERPARAM)
{
#ifdef DEBUG
	printf("%s\n", __PRETTY_FUNCTION__);
#endif
	// Do nothing for the moment...
	return 0;
}

vrpn_Tracker_PerceptionNeuron::vrpn_Tracker_PerceptionNeuron(const char *name, vrpn_Connection *c, const char* device, const char* protocol, int port)
: vrpn_Tracker(name, c)
{
#ifdef DEBUG
	printf("%s\n", __PRETTY_FUNCTION__);
#endif

	udpPort = tcpPort = -1;
	if (strcmp(protocol, "udp") == 0)
		udpPort = port;
	else
		tcpPort = port;

	strcpy(ipNeuron, device);

	// Register the handler for all messages
	register_autodeleted_handler( 
		c->register_message_type(vrpn_got_first_connection),
		handle_first_connection_message, this);
	register_autodeleted_handler(
		c->register_message_type(vrpn_got_connection),
		handle_got_connection_message, this);
	register_autodeleted_handler(
		c->register_message_type(vrpn_dropped_connection),
		handle_connection_dropped_message, this);
	register_autodeleted_handler(
		c->register_message_type(vrpn_dropped_last_connection),
		handle_dropped_last_connection_message, this);
	
}

//
vrpn_Tracker_PerceptionNeuron::~vrpn_Tracker_PerceptionNeuron()
{
#ifdef DEBUG
	printf("%s\n", __PRETTY_FUNCTION__);
#endif
	enableTracker(false);
}

bool vrpn_Tracker_PerceptionNeuron::enableTracker(bool enable)
{
#ifdef DEBUG
	printf("%s\n", __PRETTY_FUNCTION__);
#endif
	bool resp = false;
	if (enable)
	{
		resp = PerceptionNeuronHandler::getInstance()->enable(this, ipNeuron, tcpPort, udpPort);

	}
	else
	{
		resp = PerceptionNeuronHandler::getInstance()->disable();
	}
	return resp;
}

void vrpn_Tracker_PerceptionNeuron::handleData(BvhDataHeader* header, float* data)
{
	int dataIndex = 0;
	int nr = MAX_NUM_SENSORS_PER_SUIT;
	q_type destQuat;
	q_type qX, qY, qZ;

	if (!d_connection)
		return;

	// From the NeuronDataReader Runtime API Documentation_D16.pdf
	// and Cdemo_MFCDlg::showBvhBoneInfo

	// Server doesn't send neither displacement nor reference
	for (int curSel = 0; curSel < nr; curSel++)
	{
		if (header->WithDisp)
		{
			dataIndex = curSel * 6;
			if (header->WithReference)
			{
				dataIndex += 6;
			}

			// Displacement
			pos[0] = data[dataIndex + 0] * SCALE_AXIS;
			pos[1] = data[dataIndex + 1] * SCALE_AXIS;
			pos[2] = data[dataIndex + 2] * SCALE_AXIS;

			// After some checking on how Quaternion works in both Unity and VRPN
			/*
			q_from_euler(destQuat, Q_DEG_TO_RAD(data[dataIndex + 4]), Q_DEG_TO_RAD(data[dataIndex + 3]), Q_DEG_TO_RAD(data[dataIndex + 5]));
			d_quat[Q_X] = -destQuat[Q_Y];
			d_quat[Q_Y] = -destQuat[Q_Z];
			d_quat[Q_Z] = destQuat[Q_X];
			d_quat[Q_W] = destQuat[Q_W];
			*/
			{
				q_make(qX, 1.0, 0.0, 0.0, Q_DEG_TO_RAD(data[dataIndex + 3]));
				q_make(qY, 0.0, 1.0, 0.0, Q_DEG_TO_RAD(data[dataIndex + 4]));
				q_make(qZ, 0.0, 0.0, 1.0, Q_DEG_TO_RAD(data[dataIndex + 5]));
				q_mult(destQuat, qY, qX);
				q_mult(destQuat, destQuat, qZ);
				d_quat[Q_X] = destQuat[Q_X];
				d_quat[Q_Y] = destQuat[Q_Y];
				d_quat[Q_Z] = destQuat[Q_Z];
				d_quat[Q_W] = destQuat[Q_W];
			}
			if (header->AvatarIndex == 0)
			{
				d_sensor = curSel;
			}
			else
			{
				d_sensor = curSel + 60;
			}
		}
		else // ! (header->WithDisp)
		{
			if (curSel == 0)
			{
				dataIndex = 3;
				if (header->WithReference)
				{
					dataIndex += 6;
				}

				// Displacement. Send the Euler Angles, adjusted to the signs changed at the VRPNTracker.cs
				pos[0] = data[dataIndex + 0];
				pos[1] = data[dataIndex + 1];
				pos[2] = -data[dataIndex + 2];

				//Not considering rotation in the quaternion
				d_quat[0] = 0;
				d_quat[1] = 0;
				d_quat[2] = 0;
				d_quat[3] = 1;

				if (header->AvatarIndex == 0)
				{
					d_sensor = curSel;
				}
				else
				{
					d_sensor = curSel + 60;
				}
			}
			else // !(curSel == 0)
			{
				//dataIndex = curSel * 3;
				dataIndex = 3 + curSel * 3;
				if (header->WithReference)
				{
					dataIndex += 6;
				}

				// Displacement. Send the Euler Angles, adjusted to the signs changed at the VRPNTracker.cs
				pos[0] = data[dataIndex + 0];
				pos[1] = data[dataIndex + 1];
				pos[2] = -data[dataIndex + 2];

				//Not considering rotation in the quaternion
				d_quat[0] = 0;
				d_quat[1] = 0;
				d_quat[2] = 0;
				d_quat[3] = 1;

				if (header->AvatarIndex == 0)
				{
					d_sensor = curSel;
				}
				else
				{
					d_sensor = curSel + 60;
				}
			}
		}

		// send time out in Neuron's time? Not implemented yet...
		// if (frequency) frame_to_time(frame, frequency, timestamp);
		// else memset(&timestamp, 0, sizeof(timestamp));
		vrpn_gettimeofday(&timestamp, NULL);

		//send the report
		send_report();
	}

	// Add a tracker with the hips position
	dataIndex = 0;
	if (header->WithReference)
	{
		dataIndex += 6;
	}
	// Hips' Displacement
	pos[0] = -data[dataIndex + 0] * SCALE_AXIS;
	pos[1] = data[dataIndex + 1] * SCALE_AXIS;
	pos[2] = -data[dataIndex + 2] * SCALE_AXIS;
	//Not considering rotation in the quaternion
	d_quat[0] = 0;
	d_quat[1] = 0;
	d_quat[2] = 0;
	d_quat[3] = 1;

	if (header->AvatarIndex == 0)
	{
		d_sensor = nr;
	}
	else
	{
		d_sensor = nr+60;
	}
	// send time out in Neuron's time? Not implemented yet...
	vrpn_gettimeofday(&timestamp, NULL);
	//send the report
	send_report();

}


void vrpn_Tracker_PerceptionNeuron::send_report(void)
{
	const int VRPN_PERCEPTION_NEURON_MSGBUFSIZE = 1000;

	if (d_connection)
	{
		char	msgbuf[VRPN_PERCEPTION_NEURON_MSGBUFSIZE];
		int	len = encode_to(msgbuf);
		if (d_connection->pack_message(len, timestamp, position_m_id, d_sender_id, msgbuf,
			vrpn_CONNECTION_LOW_LATENCY)) {
			fprintf(stderr, "Perception Neuron: cannot write message: tossing\n");
		}
	}
}


void vrpn_Tracker_PerceptionNeuron::mainloop()
{
	PerceptionNeuronHandler::getInstance()->processData();
	return;
}


#endif
