#ifdef WIN32
#include <windows.h>
#endif
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <sys/wait.h>
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

	// Spawn a login shell with stdin/stdout/stderr connected to pipes
	int stdin_pipe[2];   // write end -> shell stdin
	int stdout_pipe[2];  // read end <- shell stdout+stderr

	pipe(stdin_pipe);
	pipe(stdout_pipe);

	pid_t pid = fork();
	if (pid == 0) {
		// Child: wire up pipes and exec shell
		dup2(stdin_pipe[0], STDIN_FILENO);
		dup2(stdout_pipe[1], STDOUT_FILENO);
		dup2(stdout_pipe[1], STDERR_FILENO);
		close(stdin_pipe[0]);
		close(stdin_pipe[1]);
		close(stdout_pipe[0]);
		close(stdout_pipe[1]);

		// Change to home directory
		const char* home = getenv("HOME");
		if (home)
			chdir(home);

		// Try bash first, fall back to sh
		execl("/bin/bash", "bash", "--login", nullptr);
		execl("/bin/sh", "sh", "-l", nullptr);
		_exit(1);
	}

	// Parent: close unused pipe ends
	close(stdin_pipe[0]);
	close(stdout_pipe[1]);

	// Forward incoming OBEX bytes (phone -> PC) to shell stdin
	std::thread incoming_thr([&]() {
		while (true) {
			auto buf = incoming.readAll(DS::BlockingPartial);
			if (buf.empty())
				break;
			write(stdin_pipe[1], buf.data(), buf.size());
		}
		close(stdin_pipe[1]);
	});

	// Forward shell stdout+stderr (PC -> phone) via OBEX outgoing stream
	std::thread outgoing_thr([&]() {
		char rbuf[1024];
		ssize_t n;
		while ((n = read(stdout_pipe[0], rbuf, sizeof(rbuf))) > 0) {
			outgoing.write(rbuf, n);
		}
		close(stdout_pipe[0]);
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
