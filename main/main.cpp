#ifdef WIN32
#include <windows.h>
#endif
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <sys/wait.h>
#include <pty.h>
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

	// OBEXServer decodes incoming bytes and writes them into stream_writer (a StreamAgent).
	// We provide the Stream that stream_writer writes into, so we can read them out.
	DS::Stream incoming;
	OBEXs.stream_writer.sdsa_connect(&incoming);  // OBEXServer → incoming (we read)

	// OBEXClient reads bytes to encode from stream_reader (a Stream).
	// We connect a StreamAgent to that Stream so we can write shell output into it.
	DS::StreamAgent outgoing;
	outgoing.sdsa_connect(&OBEXc.stream_reader);  // outgoing (we write) → OBEXClient

	OBEXc.connet();
	OBEXc.initPutStream("terminal.txt", 0x7FFFFFFF);

	OBEXs.run();
	OBEXc.run();

	// Spawn a login shell connected to a PTY so bash thinks it has a real terminal.
	// This prevents bash from emitting raw terminal sequences like \r cursor-moves
	// that confuse the phone's console parser and cause crashes.
	int master_fd = -1;
	pid_t pid = forkpty(&master_fd, nullptr, nullptr, nullptr);

	if (pid == 0) {
		// Child: we are already connected to the slave PTY side.
		// Change to home directory
		const char* home = getenv("HOME");
		if (home)
			chdir(home);

		// Disable echo on the PTY so typed commands don't get echoed back
		// (the phone handles its own echo display).
		// We can't easily do this before exec, so start a non-interactive shell.
		// Use --noediting to avoid readline escape sequences.
		execl("/bin/bash", "bash", "--login", "--noediting", nullptr);
		execl("/bin/sh", "sh", "-l", nullptr);
		_exit(1);
	}

	// Parent: master_fd is the PTY master — read shell output, write shell input.

	// Forward incoming OBEX bytes (phone → PC) to shell PTY,
	// stripping \r so that CR+LF line endings become plain LF.
	std::thread incoming_thr([&]() {
		while (true) {
			auto buf = incoming.readAll(DS::BlockingPartial);
			if (buf.empty())
				break;
			// Strip \r characters before forwarding to the shell
			std::vector<uint8_t> clean;
			clean.reserve(buf.size());
			for (uint8_t c : buf) {
				if (c != '\r')
					clean.push_back(c);
			}
			if (!clean.empty())
				write(master_fd, clean.data(), clean.size());
		}
		// Signal shell stdin EOF
		close(master_fd);
	});

	// Forward PTY output (shell stdout+stderr) → phone via OBEX outgoing stream.
	// The PTY converts \n to \r\n automatically, so the phone gets proper line endings.
	std::thread outgoing_thr([&]() {
		char rbuf[1024];
		ssize_t n;
		while ((n = read(master_fd, rbuf, sizeof(rbuf))) > 0) {
			outgoing.write(rbuf, n);
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

	waitpid(pid, nullptr, 0);

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
