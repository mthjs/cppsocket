#define CATCH_CONFIG_MAIN

#include <cppsocket.hpp>

#include <catch2/catch.hpp>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

int split_lines(const std::string& str)
{
   const std::string delimiter("\n");
   int pos = str.find(delimiter);
   if (pos != std::string::npos)
      pos += delimiter.length();
   return pos;
}

typedef int(*SplitFunc)(const std::string&);

struct Scanner
{
   Scanner(std::shared_ptr<Reader> reader)
      : __reader(reader)
      , __split(&split_lines)
   {}

   ~Scanner()
   {}

   void split(SplitFunc f)
   {
      __split = f;
   }

   bool scan()
   {
      for (;;) {
         int pos = __split(__buffer);
         if (pos != std::string::npos) {
            __scanned = __buffer.substr(0, pos);
            __buffer = __buffer.substr(pos, __buffer.length());
            return true;
         }
         std::vector<uint8_t> chunk(1024);
         auto read = __reader->read(chunk);
         __erred = read.erred();
         if (__erred) {
            __exception = read.exception();
            return false;
         }
         chunk.resize(read.get());
         __buffer += std::string(chunk.begin(), chunk.end());
      }
   }

   const std::string& text() const
   {
      if (__erred) std::rethrow_exception(__exception);
      return __scanned;
   }

   const std::exception_ptr exception() const
   {
      if (!__erred) return nullptr;
      return __exception;
   }

private:
   std::shared_ptr<Reader> __reader;
   SplitFunc __split;

   std::string __buffer;
   bool __erred;
   union {
      std::string __scanned;
      std::exception_ptr __exception;
   };
};

void require_matching_addresses(const std::shared_ptr<Connection>& local, const std::shared_ptr<Connection>& remote)
{
   REQUIRE(local->remote_addr() == remote->local_addr());
   REQUIRE(local->local_addr() == remote->remote_addr());
}

template <typename T>
void require_not_erred(Expected<T> expectation)
{
   try {
      expectation.get();
   } catch (const std::exception& e) {
      std::cerr << "expected not to err but did with: \"" << e.what() << "\"" << std::endl;
   }
   REQUIRE(expectation.erred() == false);
   REQUIRE(expectation.exception() == nullptr);
}

TEST_CASE("a TCP listener accepts new TCP connections", "[listen_tcp]") {
   SECTION("which can be read from") {
      const std::string addr = "tcp://127.0.0.1:9876";
      const std::vector<uint8_t> data{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

      std::thread listener([addr, data](){
         auto listener = listen_tcp(addr);
         auto accepted = listener->accept(std::chrono::seconds(1));
         require_not_erred(accepted);
         auto conn = accepted.get();
         std::vector<uint8_t> buffer(1024);
         auto read = conn->read(buffer, std::chrono::seconds(1));
         require_not_erred(read);
         REQUIRE(read.get() == data.size());
         REQUIRE(buffer != data);
         buffer.resize(read.get());
         REQUIRE(buffer == data);
      });
      // Let the listener thread spin up.
      std::this_thread::sleep_for(std::chrono::milliseconds(200));

      auto conn = dial_tcp(addr);
      auto written = conn->write(data);
      require_not_erred(written);
      REQUIRE(written.get() == data.size());

      listener.join();
   }

   SECTION("from which we can read in chunks") {
      const std::string addr = "tcp://127.0.0.1:8765";
      const std::vector<uint8_t> data{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

      auto listener = listen_tcp(addr);

      auto conn = dial_tcp(addr);
      auto written = conn->write(data);
      require_not_erred(written);
      REQUIRE(written.get() == data.size());
      auto accepted = listener->accept(std::chrono::seconds(1));
      require_not_erred(accepted);
      auto peer = accepted.get();
      std::vector<uint8_t> buffer;
      constexpr int steps = 2;
      for (int i = 0; i < steps; i++) {
         std::vector<uint8_t> chunk(data.size()/steps);
         auto read = peer->read(chunk, std::chrono::seconds(1));
         require_not_erred(read);
         REQUIRE(read.get() == chunk.size());
         buffer.insert(buffer.end(), chunk.begin(), chunk.end());
      }
      REQUIRE(buffer == data);
   }

   SECTION("which can be written to") {
      const std::string addr = "tcp://127.0.0.1:7654";
      const std::vector<uint8_t> data{9, 8, 7, 6, 5, 4, 3, 2, 1, 0};

      std::thread listener([addr, data](){
         auto listener = listen_tcp(addr);
         auto accepted = listener->accept(std::chrono::seconds(1));
         require_not_erred(accepted);
         auto conn = accepted.get();
         auto written = conn->write(data);
         require_not_erred(written);
      });

      // Let the listener thread spin up.
      std::this_thread::sleep_for(std::chrono::milliseconds(200));

      auto conn = dial_tcp(addr);
      std::vector<uint8_t> buffer(1024);
      auto read = conn->read(buffer, std::chrono::seconds(1));
      require_not_erred(read);
      REQUIRE(read.get() == data.size());
      REQUIRE(buffer != data);
      buffer.resize(read.get());
      REQUIRE(buffer == data);

      listener.join();
   }

   SECTION("which can be written to and read from concurrently") {
      const std::string addr = "tcp://127.0.0.1:6543";
      const std::string hello = "hello?\n";
      const std::string ping = "ping?\n";
      const std::string pong = "pong!\n";

      auto shout = [](std::shared_ptr<Connection> conn, std::string what, int n, std::chrono::milliseconds p)
      {
         const std::vector<uint8_t> buffer(what.begin(), what.end());
         for (int i = 0; i < n; i++) {
            auto written = conn->write(buffer);
            require_not_erred(written);
            REQUIRE(written.get() == buffer.size());
            std::this_thread::sleep_for(p);
         }
      };

      std::thread listener([=](){
         auto listener = listen_tcp(addr);
         auto accepted = listener->accept(std::chrono::seconds(1));
         require_not_erred(accepted);
         auto conn = accepted.get();
         std::thread pinging(shout, conn, ping, 20, std::chrono::milliseconds(25));
         std::thread helloing(shout, conn, hello, 10, std::chrono::milliseconds(50));

         int pongs = 0;
         Scanner scanner(conn);
         while (scanner.scan()) {
            if (scanner.text() == pong)
               pongs++;
            if (pongs == 20)
               break;
         }
         REQUIRE(pongs == 20);

         pinging.join();
         helloing.join();
      });

      // Let the listener thread spin up.
      std::this_thread::sleep_for(std::chrono::milliseconds(200));

      std::thread client([addr, hello, ping, pong](){
         auto conn = dial_tcp(addr);
         int hellos = 0;
         int pings = 0;
         Scanner scanner(conn);
         while (scanner.scan()) {
            if (scanner.text() == hello) {
               hellos++;
            } else if (scanner.text() == ping) {
               pings++;
               std::vector<uint8_t> buffer(pong.begin(), pong.end());
               auto written = conn->write(buffer);
               require_not_erred(written);
               REQUIRE(written.get() == buffer.size());
            }
            if (hellos == 10 && pings == 20)
               break;
         }
         REQUIRE(hellos == 10);
         REQUIRE(pings == 20);
      });

      listener.join();
      client.join();
   }

   SECTION("which has the client's address as its remote_addr") {
      const std::string addr = "tcp://127.0.0.1:5432";

      auto listener = listen_tcp(addr);
      auto conn = dial_tcp(addr);
      auto accepted = listener->accept(std::chrono::seconds(1));
      require_not_erred(accepted);
      auto connected = accepted.get();
      require_matching_addresses(conn, connected);
   }
}

TEST_CASE("listen_udp yields a UDP connection", "[listen_udp]") {
   SECTION("which listens for any UDP client") {
      const std::string listener = "udp://127.0.0.1:9999";
      const std::string server = "udp://127.0.0.1:9998";
      const std::string hola = "hola!\n";

      auto serving = listen_udp(server);
      auto primero = dial_udp(server);
      auto segundo = dial_udp(server);

      {
         const std::vector<uint8_t> wbuffer(hola.begin(), hola.end());
         auto written = primero->write(wbuffer, server);
         require_not_erred(written);
         REQUIRE(written.get() == wbuffer.size());

         std::string remote;
         std::vector<uint8_t> buffer(1024);
         auto read = serving->read(buffer, remote);
         require_not_erred(read);
         REQUIRE(remote == primero->local_addr());
         buffer.resize(read.get());
         serving->write(buffer, remote);

         std::vector<uint8_t> rbuffer(1024);
         std::cout << "primero is reading" << std::endl;
         auto responded = primero->read(rbuffer, std::chrono::seconds(1));
         require_not_erred(responded);
         rbuffer.resize(responded.get());
         REQUIRE(wbuffer == rbuffer);
      }

      {
         // writing without a specified address should still work
         const std::vector<uint8_t> wbuffer(hola.begin(), hola.end());
         auto written = segundo->write(wbuffer);
         require_not_erred(written);
         REQUIRE(written.get() == wbuffer.size());

         std::string remote;
         std::vector<uint8_t> buffer(1024);
         auto read = serving->read(buffer, remote, std::chrono::seconds(1));
         require_not_erred(read);
         REQUIRE(remote == segundo->local_addr());
      }

      auto listening = listen_udp(listener);
      {
         // writing to a different address should also work
         const std::vector<uint8_t> wbuffer(hola.begin(), hola.end());
         auto written = segundo->write(wbuffer, listener);
         require_not_erred(written);
         REQUIRE(written.get() == wbuffer.size());

         std::string remote;
         std::vector<uint8_t> buffer(1024);
         std::cout << "listening is gonna read segundo" << std::endl;
         auto read = listening->read(buffer, remote, std::chrono::seconds(1));
         require_not_erred(read);
         REQUIRE(remote == segundo->local_addr());
      }
   }
}
