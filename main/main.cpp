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
#include <termios.h>
#include <BTSockListener.h>
#include <BTAdapter.h>
#include <OBEXServer.h>
#include <OBEXClient.h>
#include <Stream.h>
#include <StreamAgent.h>


uint16_t obex_id = 0x1105;

// Maximum bytes to send to the phone per chunk.
// The phone's OBEX receive buffer is 64KB but its timer reads only 100 bytes
// every 33ms (~3000 bytes/sec). Keep chunks small to avoid overwhelming it.
static const size_t MAX_CHUNK = 64;

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

	DS::Stream incoming;
	OBEXs.stream_writer.sdsa_connect(&incoming);

	DS::StreamAgent outgoing;
	outgoing.sdsa_connect(&OBEXc.stream_reader);

	OBEXc.connet();
	OBEXc.initPutStream("terminal.txt", 0x7FFFFFFF);

	OBEXs.run();
	OBEXc.run();

	// Spawn a login shell connected to a PTY.
	int master_fd = -1;
	pid_t pid = forkpty(&master_fd, nullptr, nullptr, nullptr);

	if (pid == 0) {
		// Child: connected to the slave PTY side.
		const char* home = getenv("HOME");
		if (home)
			chdir(home);

		// TERM=dumb: no ANSI colour or cursor-movement sequences.
		setenv("TERM", "dumb", 1);
		setenv("BASH_ENV", "", 1);

		execl("/bin/bash", "bash", "--login", "--noediting", nullptr);
		execl("/bin/sh", "sh", "-l", nullptr);
		_exit(1);
	}

	// Parent: configure the PTY master.
	{
		struct termios t;
		if (tcgetattr(master_fd, &t) == 0) {
			// Disable echo so typed characters aren't doubled on the phone.
			t.c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHONL);
			// Disable ONLCR (NL→CR+LF translation).
			// We do our own \n→\r\n conversion before sending to the phone,
			// so the phone always gets proper CR+LF line endings and blank
			// Enter (which sends just \n) produces a visible new line.
			t.c_oflag &= ~ONLCR;
			tcsetattr(master_fd, TCSANOW, &t);
		}
	}

	// Forward incoming OBEX bytes (phone → PC) to shell PTY,
	// stripping \r so that CR+LF line endings become plain LF.
	std::thread incoming_thr([&]() {
		while (true) {
			auto buf = incoming.readAll(DS::BlockingPartial);
			if (buf.empty())
				break;
			std::vector<uint8_t> clean;
			clean.reserve(buf.size());
			for (uint8_t c : buf) {
				if (c != '\r')
					clean.push_back(c);
			}
			if (!clean.empty())
				write(master_fd, clean.data(), clean.size());
		}
		close(master_fd);
	});

	// Forward PTY output (shell stdout+stderr) → phone via OBEX.
	// Convert \n → \r\n so the phone's console advances lines correctly,
	// including after a blank Enter where bash only outputs \n.
	// Send at most MAX_CHUNK bytes at a time with a 35ms pause to pace output.
	std::thread outgoing_thr([&]() {
		char rbuf[MAX_CHUNK];
		ssize_t n;
		while ((n = read(master_fd, rbuf, sizeof(rbuf))) > 0) {
			// Expand \n to \r\n before sending to phone.
			std::vector<char> out;
			out.reserve(n * 2);
			for (ssize_t i = 0; i < n; ++i) {
				if (rbuf[i] == '\n') {
					out.push_back('\r');
				}
				out.push_back(rbuf[i]);
			}
			outgoing.write(out.data(), out.size());
			// Pace output: give the phone ~1 timer tick (33ms) to process
			// each chunk before sending more.
			std::this_thread::sleep_for(std::chrono::milliseconds(35));
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
