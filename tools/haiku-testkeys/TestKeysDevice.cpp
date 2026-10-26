// A development-only input_server device: whatever BMessages arrive on the
// "haiku pinyin test keys" port go into the input_server as if typed, so tests
// drive the whole path (input methods included) from ssh. Use hpkeys.

#include <InputServerDevice.h>
#include <Message.h>
#include <OS.h>

#include <new>
#include <stdlib.h>


class TestKeysDevice : public BInputServerDevice {
public:
	TestKeysDevice()
	{
		fPort = create_port(64, "haiku pinyin test keys");
		fThread = spawn_thread(_Read, "haiku pinyin test keys", B_NORMAL_PRIORITY,
			this);
		resume_thread(fThread);
	}

	virtual ~TestKeysDevice()
	{
		delete_port(fPort);
		status_t result;
		wait_for_thread(fThread, &result);
	}

	virtual status_t InitCheck()
	{
		return fPort >= 0 ? B_OK : fPort;
	}

private:
	static status_t _Read(void* data)
	{
		TestKeysDevice* device = (TestKeysDevice*)data;
		while (true) {
			ssize_t size = port_buffer_size(device->fPort);
			if (size < 0)
				return B_OK;	// port deleted
			char* buffer = (char*)malloc(size);
			int32 code;
			if (read_port(device->fPort, &code, buffer, size) < 0) {
				free(buffer);
				return B_OK;
			}
			BMessage* message = new BMessage;
			if (message->Unflatten(buffer) == B_OK)
				device->EnqueueMessage(message);
			else
				delete message;
			free(buffer);
		}
	}

	port_id fPort;
	thread_id fThread;
};


extern "C" BInputServerDevice*
instantiate_input_device()
{
	return new(std::nothrow) TestKeysDevice();
}
