#include <cppsocket.hpp>

namespace sys {

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

}

#include <cstring>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

/**
 * netaddr attempts to deduce the IP and port for the given `sockaddr`.
 */
static Expected<std::string> netaddr(const struct sys::sockaddr* sa)
{
   void *saina;
   int port;
   if (sa->sa_family == AF_INET) {
      struct sys::sockaddr_in *sai = (struct sys::sockaddr_in *)&sa;
      port = sys::ntohs(sai->sin_port);
      saina = &(((struct sys::sockaddr_in*)sa)->sin_addr);
   } else if (sa->sa_family == AF_INET6){
      struct sys::sockaddr_in6 *sai = (struct sys::sockaddr_in6 *)&sa;
      port = sys::ntohs(sai->sin6_port);
      saina = &(((struct sys::sockaddr_in6*)sa)->sin6_addr);
   } else {
      return Expected<std::string>::unexpected(std::runtime_error("netaddr: unsupported family"));
   }
   char str[INET6_ADDRSTRLEN];
   if (sys::inet_ntop(sa->sa_family, saina, str, sizeof(str)) == NULL)
      return Expected<std::string>::unexpected(std::runtime_error(
         std::string("netaddr: unable to convert IP to human-readable form - ") + std::strerror(errno)
      ));
   return std::string(str) + ":" + std::to_string(port);
}

/**
 * netaddr attempts to deduce the IP and port for the given socket reference.
 */
static Expected<std::string> netaddr(int socket)
{
   struct sys::sockaddr_storage sas;
   sys::socklen_t sasl(sizeof(sas));
   if (getsockname(socket, (struct sys::sockaddr*)&sas, &sasl) == -1)
      return Expected<std::string>::unexpected(std::runtime_error(
         std::string("netaddr: unable to aquire localaddr - ") + std::strerror(errno)
      ));
   return netaddr((struct sys::sockaddr*)&sas);
}

/**
 * Snipper is a callable object that snips a part, up to the position of the
 * provided delimiter, on each call and returns the snipped part which can
 * either be a `const char*` or `NULL`.
 *
 * It is required to provide an estimate snips, to make sure the actual data
 * doesn't get lost.
 */
struct Snipper
{
   Snipper(const std::string& str, int snips)
      : __str(str)
   {
      __snipped.reserve(snips);
   }

   const char* remaining_or(const char* c) const
   {
      return __str != "" ? __str.c_str() : c;
   }

   const char* operator()(const std::string& delimiter)
   {
      int pos = __str.find(delimiter);
      if (pos == std::string::npos)
         return NULL;
      __snipped.push_back(std::string(__str.substr(0, pos)));
      __str = __str.substr(pos+delimiter.length(), __str.length());
      return __snipped.at(__snipped.size()-1).c_str();
   }

private:
   std::string __str;
   std::vector<std::string> __snipped;
};

static Expected<std::shared_ptr<struct sys::addrinfo>> resolve(const std::string& address)
{
   Snipper snip(address, 3);
   const char* protocol = snip("://");
   const char* hostname = snip(":");
   const char* port = snip.remaining_or("80");
   struct sys::addrinfo hints, *resolved;
   memset(&hints, 0, sizeof(hints));
   hints.ai_family = AF_UNSPEC;
   if (std::string(protocol) == "tcp")
      hints.ai_socktype = sys::SOCK_STREAM;
   else if (std::string(protocol) == "udp")
      hints.ai_socktype = sys::SOCK_DGRAM;
   else
      return Expected<std::shared_ptr<struct sys::addrinfo>>::unexpected(std::runtime_error(
         std::string("resolve: unable to resolve \"") + address + "\" - Unsupported protocol \"" + protocol + "\""
      ));
   int status = sys::getaddrinfo(hostname, port, &hints, &resolved);
   if (status != 0)
      return Expected<std::shared_ptr<struct sys::addrinfo>>::unexpected(std::runtime_error(
         std::string("resolve: trying to resolve \"") + address + "\" but failed - " + sys::gai_strerror(status)
      ));
   return std::shared_ptr<struct sys::addrinfo>(resolved, sys::freeaddrinfo);
}

struct UDPConnectionImpl
   : UDPConnection
{
   // Sending
   UDPConnectionImpl(const std::shared_ptr<struct sys::addrinfo>& resolved, const std::string& dialing)
   {
      __socket = sys::socket(resolved->ai_family, resolved->ai_socktype, resolved->ai_protocol);
      if (__socket == -1)
         throw std::runtime_error(
            std::string("UDPConnection::UDPConnection: unable to acquire socket - ") +
            std::strerror(errno)
         );
      if (sys::connect(__socket, resolved->ai_addr, resolved->ai_addrlen) == -1)
         throw std::runtime_error(
            std::string("UDPConnection::UDPConnection: unable to connect socket - ") +
            std::strerror(errno)
         );
      __local_addr = std::string("udp://") + netaddr(__socket).get();
      __remote_addr = dialing;
   }

   // Receiving
   UDPConnectionImpl(const std::shared_ptr<struct sys::addrinfo>& resolved)
   {
      __socket = sys::socket(resolved->ai_family, resolved->ai_socktype, resolved->ai_protocol);
      if (__socket == -1)
         throw std::runtime_error(
            std::string("UDPConnection::UDPConnection: unable to acquire socket - ") +
            std::strerror(errno)
         );
      if (sys::bind(__socket, resolved->ai_addr, resolved->ai_addrlen) == -1)
         throw std::runtime_error(
            std::string("UDPConnection::UDPConnection: unable to bind socket - ") +
            std::strerror(errno)
         );
      __local_addr = (std::string("udp://") + netaddr(resolved->ai_addr).get());
      __remote_addr = unknown_addr;
   }

   void timeout(const std::chrono::microseconds& t)
   {
      read_timeout(t);
      write_timeout(t);
   }

   void read_timeout(const std::chrono::microseconds& t)
   {
      const std::chrono::seconds s = std::chrono::duration_cast<std::chrono::seconds>(t);
      struct timeval tv;
      tv.tv_sec = s.count();
      tv.tv_usec = (t - s).count();
      if (sys::setsockopt(__socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv))  == -1)
         throw new std::runtime_error(
            std::string("UDPConnection::read_timeout: unable to set read timeout - ") +
            std::strerror(errno)
         );
   }

   void write_timeout(const std::chrono::microseconds& t)
   {
      const std::chrono::seconds s = std::chrono::duration_cast<std::chrono::seconds>(t);
      struct timeval tv;
      tv.tv_sec = s.count();
      tv.tv_usec = (t - s).count();
      if (sys::setsockopt(__socket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == -1)
         throw new std::runtime_error(
            std::string("UDPConnection::read_timeout: unable to set write timeout - ") +
            std::strerror(errno)
         );
   }

   std::string local_addr() const noexcept
   {
      return __local_addr;
   }

   std::string remote_addr() const noexcept
   {
      return __remote_addr;
   }

   Expected<size_t> read(std::vector<uint8_t>& b, std::string& remote, const std::chrono::milliseconds& t)
   {
      {
         struct sys::pollfd pfd;
         pfd.fd = __socket;
         pfd.events = POLLIN;
         int result = poll(&pfd, 1, t.count());
         if (result == -1 || pfd.revents & POLLERR)
            return Expected<size_t>::unexpected(std::runtime_error(std::string("UDPConnection::read: failed to poll the socket - ") + std::strerror(errno)));
         if (result == 0)
            return Expected<size_t>::unexpected(std::logic_error("UDPConnection::read: timeout whilst polling the socket"));
      }

      struct sys::sockaddr_storage sas;
      sys::socklen_t sasl(sizeof(sas));
      ssize_t s = sys::recvfrom(__socket, &b[0], b.size(), 0, (struct sys::sockaddr *)&sas, &sasl);
      if (s < 0)
         return Expected<size_t>::unexpected(std::runtime_error(std::string("UDPConnection::read: unable to read - ") + std::strerror(errno)));
      auto from = netaddr((struct sys::sockaddr*)&sas);
      remote = from.erred() ? unknown_addr : std::string("udp://") + from.get();
      return s;
   }

   Expected<size_t> read(std::vector<uint8_t>& b, std::string& remote)
   {
      return read(b, remote, std::chrono::milliseconds(-1));
   }

   Expected<size_t> read(std::vector<uint8_t>& b, const std::chrono::milliseconds& t)
   {
      if (__remote_addr != unknown_addr)
         return Expected<size_t>::unexpected(std::logic_error(
            "UDPConnection::read: reading from a sending UDP connection without addressee"
         ));
      std::string whom;
      return read(b, whom, t);
   }

   Expected<size_t> read(std::vector<uint8_t>& b)
   {
      return read(b, std::chrono::milliseconds(-1));
   }

   Expected<size_t> write(const std::vector<uint8_t>& b, const std::string& remote, const std::chrono::milliseconds& t)
   {
      auto resolved = __resolve(remote);
      if (resolved.erred())
         return Expected<size_t>::unexpected(std::invalid_argument(std::string("UDPConnection::write: unable to resolve the given remote \"") + remote + "\""));

      {
         struct sys::pollfd pfd;
         pfd.fd = __socket;
         pfd.events = POLLOUT;
         int result = poll(&pfd, 1, t.count());
         if (result == -1 || pfd.revents & POLLERR)
            return Expected<size_t>::unexpected(std::runtime_error(std::string("UDPConnection::writes: failed to poll the socket - ") + std::strerror(errno)));
         if (result == 0)
            return Expected<size_t>::unexpected(std::logic_error("UDPConnection::write: timeout whilst polling the socket"));
      }

      auto to = resolved.get();
      ssize_t s = sys::sendto(__socket, &b[0], b.size(), 0, to->ai_addr, to->ai_addrlen);
      if (s < 0)
         return Expected<size_t>::unexpected(std::runtime_error(std::string("UDPConnection::write: unable to write - ") + std::strerror(errno)));
      return s;
   }

   Expected<size_t> write(const std::vector<uint8_t>& b, const std::string& remote)
   {
      return write(b, remote, std::chrono::milliseconds(-1));
   }

   Expected<size_t> write(const std::vector<uint8_t>& b, const std::chrono::milliseconds& t)
   {
      if (__remote_addr == unknown_addr)
         return Expected<size_t>::unexpected(std::logic_error(
            "UDPConnection::write: writing to receiving UDP connection without addressee"
         ));
      return write(b, __remote_addr, t);
   }

   Expected<size_t> write(const std::vector<uint8_t>& b)
   {
      return write(b, std::chrono::milliseconds(-1));
   }

private:
   Expected<std::shared_ptr<struct sys::addrinfo>> __resolve(const std::string& remote)
   {
      {
         std::lock_guard<std::mutex> lock(__remotes_lock);
         auto found = __remotes.find(remote);
         if (found != __remotes.end())
            return found->second;
      }
      auto resolved = resolve(remote);
      if (resolved.erred())
         return resolved.exception();
      {
         std::lock_guard<std::mutex> lock(__remotes_lock);
         __remotes[remote] = resolved.get();
      }
      return resolved.get();
   }

private:
   int __socket;
   std::string __local_addr;
   std::string __remote_addr;
   // would be nicer to have a LRU-cache with lookup instead of this thing
   // that'll grow indefinitely.
   std::unordered_map<std::string, std::shared_ptr<struct sys::addrinfo>> __remotes;
   std::mutex __remotes_lock;
};

std::shared_ptr<UDPConnection> listen_udp(const std::string& address)
{
   auto resolved = resolve(address).get();
   if (resolved->ai_socktype != sys::SOCK_DGRAM)
      throw std::runtime_error(
         std::string("listen_udp: attempting to use a non-UDP socket on \"") + address + "\""
      );
   return std::make_shared<UDPConnectionImpl>(resolved);
}

std::shared_ptr<UDPConnection> dial_udp(const std::string& address)
{
   auto resolved = resolve(address).get();
   if (resolved->ai_socktype != sys::SOCK_DGRAM)
      throw std::runtime_error(
         std::string("dial_udp: attempting to use a non-UDP socket on \"") + address + "\""
      );
   return std::make_shared<UDPConnectionImpl>(resolved, address);
}

struct TCPConnectionImpl
   : TCPConnection
{
   TCPConnectionImpl(const std::shared_ptr<struct sys::addrinfo>& resolved)
      : __remote_addr(std::string("tcp://") + netaddr(resolved->ai_addr).get())
   {
      __socket = sys::socket(resolved->ai_family, resolved->ai_socktype, resolved->ai_protocol);
      if (__socket == -1)
         throw std::runtime_error(
            std::string("TCPConnection::TCPConnection: unable to acquire socket - ") +
            std::strerror(errno)
         );
      if (sys::connect(__socket, resolved->ai_addr, resolved->ai_addrlen) < 0) {
         sys::close(__socket);
         throw std::runtime_error(
            std::string("TCPConnection::TCPConnection: unable to connect socket - ") +
            std::strerror(errno)
         );
      }
      __local_addr = std::string("tcp://") + netaddr(__socket).get();
   }

   TCPConnectionImpl(int socket, const std::string& localaddr, const std::string& remote)
      : __socket(socket)
      , __local_addr(std::string("tcp://") + localaddr)
      , __remote_addr(std::string("tcp://") + remote)
   {}

   ~TCPConnectionImpl()
   {
      sys::close(__socket);
   }

   void timeout(const std::chrono::microseconds& t)
   {
      read_timeout(t);
      write_timeout(t);
   }

   void read_timeout(const std::chrono::microseconds& t)
   {
      const std::chrono::seconds s = std::chrono::duration_cast<std::chrono::seconds>(t);
      struct timeval tv;
      tv.tv_sec = s.count();
      tv.tv_usec = (t - s).count();
      if (sys::setsockopt(__socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv))  == -1)
         throw new std::runtime_error(
            std::string("TCPConnection::read_timeout: unable to set read timeout - ") +
            std::strerror(errno)
         );
   }

   void write_timeout(const std::chrono::microseconds& t)
   {
      const std::chrono::seconds s = std::chrono::duration_cast<std::chrono::seconds>(t);
      struct timeval tv;
      tv.tv_sec = s.count();
      tv.tv_usec = (t - s).count();
      if (sys::setsockopt(__socket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == -1)
         throw new std::runtime_error(
            std::string("TCPConnection::read_timeout: unable to set write timeout - ") +
            std::strerror(errno)
         );
   }

   void no_delay(bool d)
   {
      int opt = d ? 1 : 0;
      if (sys::setsockopt(__socket, SOL_TCP, TCP_NODELAY, &opt, sizeof(opt)) == -1)
         throw new std::runtime_error(
            std::string("TCPConnection::no_delay: unable to set NODELAY - ") +
            std::strerror(errno)
         );
   }

   std::string local_addr() const noexcept
   {
      return __local_addr;
   }

   std::string remote_addr() const noexcept
   {
      return __remote_addr;
   }

   Expected<size_t> read(std::vector<uint8_t>& b, const std::chrono::milliseconds& t)
   {
      {
         struct sys::pollfd pfd;
         pfd.fd = __socket;
         pfd.events = POLLIN;
         int result = poll(&pfd, 1, t.count());
         if (result == -1 || pfd.revents & POLLERR)
            return Expected<size_t>::unexpected(std::runtime_error(
                  std::string("TCPConnection::read: failed to poll the socket - ") +
                  std::strerror(errno)
               ));
         if (result == 0)
            return Expected<size_t>::unexpected(std::logic_error(
               "TCPConnection::read: timeout whilst polling the socket"
            ));
      }

      ssize_t s = sys::read(__socket, &b[0], b.size());
      if (s < 0)
         return Expected<size_t>::unexpected(std::runtime_error(
            std::string("TCPConnection::read: unable to read - ") +
            std::strerror(errno)
         ));
      return s;
   }

   Expected<size_t> read(std::vector<uint8_t>& b)
   {
      return read(b, std::chrono::milliseconds(-1));
   }

   Expected<size_t> write(const std::vector<uint8_t>& b, const std::chrono::milliseconds& t)
   {
      {
         struct sys::pollfd pfd;
         pfd.fd = __socket;
         pfd.events = POLLOUT;
         int result = poll(&pfd, 1, t.count());
         if (result == -1 || pfd.revents & POLLERR)
            return Expected<size_t>::unexpected(std::runtime_error(
               std::string("TCPConnection::write: failed to poll the socket - ") +
               std::strerror(errno)
            ));
         if (result == 0)
            return Expected<size_t>::unexpected(std::logic_error(
               "TCPConnection::write: timeout whilst polling the socket"
            ));
      }

      ssize_t s = sys::write(__socket, &b[0], b.size());
      if (s < 0)
         return Expected<size_t>::unexpected(std::runtime_error(
            std::string("TCPConnection::write: unable to write - ") +
            std::strerror(errno)
         ));
      return s;
   }

   Expected<size_t> write(const std::vector<uint8_t>& b)
   {
      return write(b, std::chrono::milliseconds(-1));
   }

private:
   int __socket;
   std::string __local_addr;
   std::string __remote_addr;
};

struct TCPListenerImpl
   : TCPListener
{
   TCPListenerImpl(std::shared_ptr<struct sys::addrinfo> resolved)
      : __addr(resolved)
      , __timeout(std::chrono::milliseconds(-1))
   {
      __socket = sys::socket(__addr->ai_family, __addr->ai_socktype, __addr->ai_protocol);
      if (__socket == -1)
         throw std::runtime_error(
            std::string("TCPListener::TCPListener: unable to acquire socket - ") +
            std::strerror(errno)
         );
      int opt = 1;
      if (sys::setsockopt(__socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
         sys::close(__socket);
         throw std::runtime_error(
            std::string("TCPListener::TCPListener: unable to claim socket - ") +
            std::strerror(errno)
         );
      }
      if (sys::bind(__socket, __addr->ai_addr, __addr->ai_addrlen) == -1) {
         sys::close(__socket);
         throw std::runtime_error(
            std::string("TCPListener::TCPListener: unable to bind socket - ") +
            std::strerror(errno)
         );
      }
      if (sys::listen(__socket, kDefaultListenBacklog) == -1) {
         sys::close(__socket);
         throw std::runtime_error(
            std::string("TCPListener::TCPListener: unable to listen - ") +
            std::strerror(errno)
         );
      }
   }

   ~TCPListenerImpl()
   {
      sys::close(__socket);
   }

   Expected<std::shared_ptr<TCPConnection>> accept(const std::chrono::milliseconds& t)
   {
      {
         struct sys::pollfd pfd;
         pfd.fd = __socket;
         pfd.events = POLLIN;
         int result = poll(&pfd, 1, t.count());
         if (result == -1 || pfd.revents & POLLERR)
            return Expected<std::shared_ptr<TCPConnection>>::unexpected(std::runtime_error(
               std::string("TCPListener::accept: failed to poll the bound-socket - ") +
               std::strerror(errno)
            ));
      }

      struct sys::sockaddr_storage sas;
      sys::socklen_t sasl(sizeof(sas));
      int socket = sys::accept(__socket, (struct sys::sockaddr*)&sas, &sasl);
      if (socket == -1)
         return Expected<std::shared_ptr<TCPConnection>>::unexpected(std::runtime_error(
            std::string("TCPListener::accept: failed to accept a new connection - ") +
            std::strerror(errno)
         ));

      auto local_addr = netaddr(__addr->ai_addr);
      if (local_addr.erred())
         return local_addr.exception();
      auto remote_addr = netaddr((struct sys::sockaddr*)&sas);
      if (remote_addr.erred())
         return remote_addr.exception();
      std::shared_ptr<TCPConnection> conn = std::make_shared<TCPConnectionImpl>(
         socket,
         local_addr.get(),
         remote_addr.get()
      );
      return conn;
   }

   Expected<std::shared_ptr<TCPConnection>> accept()
   {
      return accept(__timeout);
   }

   void timeout(const std::chrono::milliseconds& t)
   {
      __timeout = t;
   }

private:
   std::shared_ptr<struct sys::addrinfo> __addr;
   std::chrono::milliseconds __timeout;
   int __socket;
};

std::unique_ptr<TCPListener> listen_tcp(const std::string& address)
{
   auto resolved = resolve(address).get();
   if (resolved->ai_socktype != sys::SOCK_STREAM)
      throw std::runtime_error(
         std::string("listen_tcp: attempting to use a non-TCP socket on \"") + address + "\""
      );
   return std::unique_ptr<TCPListener>(new TCPListenerImpl(resolved));
}

std::shared_ptr<TCPConnection> dial_tcp(const std::string& address)
{
   auto resolved = resolve(address).get();
   if (resolved->ai_socktype != sys::SOCK_STREAM)
      throw std::runtime_error(
         std::string("dial_tcp: attempting to use a non-TCP socket on \"") + address + "\""
      );
   return std::make_shared<TCPConnectionImpl>(resolved);
}
