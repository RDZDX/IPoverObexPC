#ifdef WIN32
#include <windows.h>
#endif
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <BTSockListener.h>
#include <BTAdapter.h>
#include <OBEXServer.h>
#include <OBEXClient.h>
#include <Stream.h>
#include <StreamAgent.h>


uint16_t obex_id = 0x1105;

void worker(BTSock btsocks, BTSock btsockc) {
	auto mac = btsocks.getRemoteAddress().toString();
	auto name = btsocks.getRemoteAddress().getName();
	std::cout << "Connected: " << name << " (" << mac << ")\n";

	OBEXServer OBEXs;
	OBEXs.reader.sdra_connect(&btsocks);
	OBEXs.writer.sdwa_connect(&btsocks);

	OBEXClient OBEXc;
	OBEXc.reader.sdra_connect(&btsockc);
	OBEXc.writer.sdwa_connect(&btsockc);

	// Bridge: OBEXServer writes decoded bytes into stream_writer (StreamAgent);
	// connect a Stream so we can read those bytes out.
	DS::Stream incoming;
	incoming.sds_connect(&OBEXs.stream_writer);

	// Bridge: OBEXClient reads bytes to send from stream_reader (Stream);
	// connect a StreamAgent so we can write bytes into it.
	DS::StreamAgent outgoing;
	outgoing.sdsa_connect(&OBEXc.stream_reader);

	OBEXc.connet();
	OBEXc.initPutStream("terminal.txt", 0x7FFFFFFF);

	OBEXs.run();
	OBEXc.run();

	// Forward incoming OBEX stream data (device -> PC) to stdout
	std::thread incoming_thr([&]() {
		while (true) {
			auto buf = incoming.readAll(DS::BlockingPartial);
			if (buf.empty())
				break;
			std::cout.write(reinterpret_cast<const char*>(buf.data()), buf.size());
			std::cout.flush();
		}
	});

	// Forward stdin (PC -> device) via OBEX client stream
	std::thread outgoing_thr([&]() {
		std::string line;
		while (std::getline(std::cin, line)) {
			line += '\n';
			outgoing.write(line.data(), line.size());
		}
		outgoing.sdsa_close();
	});

	OBEXs.wait();
	OBEXc.wait();

	incoming.sds_close();

	if (incoming_thr.joinable())
		incoming_thr.join();
	if (outgoing_thr.joinable())
		outgoing_thr.join();

	std::cout << "Disconnected: " << mac << '\n';
}

std::vector<std::thread> threads;

void wait_any_key_to_exit() {
#ifdef WIN32
	char c;
	std::cout << "Press any key to exit\n";
	std::cin >> c;
#endif

	exit(0);
}

int main() {
#ifdef WIN32
	SetConsoleOutputCP(CP_UTF8);
#endif

	BTAdapter adapter;
	if (!adapter.isThere())
		if (!adapter.isOn()) {
			std::cout << "BT is turn off, trying to turn on... ";

			if (adapter.setOn(true)) {
				std::cout << "done\n";
				std::cout << "Waiting 5 seconds for full BT initialization...\n";
				std::this_thread::sleep_for(std::chrono::seconds(5));
			}
			else {
				std::cout << "not allowed\n";
				std::cout << "Turn on BT manually and try again\n";
				wait_any_key_to_exit();
			}
		}

	std::cout << "BT name: " << adapter.getName() << '\n';
	std::cout << "BT mac: " << adapter.getAddress().toString() << "\n";

	BTSockListener btsockl;
	if (!btsockl.bind(obex_id)) {
		std::cout << "Failed to bind BT service, please close other application "
			<< "that may be using this service (OBEX/File transfer) and try again.\n";
		wait_any_key_to_exit();
	}

	std::cout << "BT start listening\n";
	while (true) {
		BTSock btsocks, btsockc;
		btsockl.accept(btsocks, true);
		btsockc.connect(btsockl.getShortId(), btsocks.getRemoteAddress());

		threads.push_back(std::thread(worker, btsocks, btsockc));
	}

	wait_any_key_to_exit();
}
