#ifndef _CPPSOCKET
#define _CPPSOCKET

#include <expected.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

struct Reader
{
   /**
    * read attempts to read data from the underlaying target into the provided
    * buffer `b`. The underlaying target is allowed to be unavailable up to a
    * duration of `t`.
    *
    * Omitting `t` or providing a negative value for `t` will block until the
    * underlaying target is available for reading.
    */
   virtual Expected<size_t> read(std::vector<uint8_t>& b, const std::chrono::milliseconds& t) = 0;
   virtual Expected<size_t> read(std::vector<uint8_t>& b) = 0;
};

struct Writer
{
   /**
    * write attempts to write the given buffer `b` and allows the underlaying
    * target to be unavailable for a duration of `t`.
    *
    * Omitting `t` or providing a negative value for `t` will block until the
    * underlaying target is available for writing.
    */
   virtual Expected<size_t> write(const std::vector<uint8_t>& b, const std::chrono::milliseconds& t) = 0;
   virtual Expected<size_t> write(const std::vector<uint8_t>& b) = 0;
};

struct Connection
   : Reader
   , Writer
{
   /**
    * local_addr returns the local address.
    */
   virtual std::string local_addr() const = 0;

   /**
    * remote_addr returns the address of the peer.
    */
   virtual std::string remote_addr() const = 0;

   /**
    * timeout, read_timeout, and write_timeout all set the actual socket
    * timeouts.
    */
   virtual void timeout(const std::chrono::microseconds& t) = 0;
   virtual void read_timeout(const std::chrono::microseconds& t) = 0;
   virtual void write_timeout(const std::chrono::microseconds& t) = 0;
};

struct TCPConnection
   : Connection
{
   /**
    * no_delay will either set the NODELAY on the TCP connection depending
    * on the value of `d`. `true` to enable, `false` to disable.
    *
    * NODELAY is disabled by default.
    */
   void no_delay(bool d);
};

/**
 * TCPListener listens for new TCP connections to accept.
 */
struct TCPListener
{
   const int kDefaultListenBacklog = 512;

   /**
    * accept listens for a new connection and returns a new Connection when
    * said connection was successfully accepted. accept will return a
    * `std::logic_error` when no data was available after the provided duration
    * `t`.
    *
    * Providing a negative duration for `t` or calling accept without `t` will
    * block indefinitely until there is a new connection available.
    *
    * The default timeout used by accept sans duration `t`, can be set through
    * the timeout function.
    */
   virtual Expected<std::shared_ptr<TCPConnection>> accept(const std::chrono::milliseconds& t) = 0;
   virtual Expected<std::shared_ptr<TCPConnection>> accept() = 0;

   /**
    * timeout allows you to change the default timeout which is used when
    * calling accept without its `t` argument.
    */
   virtual void timeout(const std::chrono::milliseconds& t) = 0;
};

/**
 * listen_tcp creates a new listener which'll start listening for TCP
 * connections on the given address.
 */
std::unique_ptr<TCPListener> listen_tcp(const std::string& address);

/**
 * dial_tcp creates a new TCP connection which'll try to connect to the given
 * address.
 */
std::shared_ptr<TCPConnection> dial_tcp(const std::string& address);

struct ReaderFrom
{
   /**
    * read attempts to read data from the underlaying target into the provided
    * buffer `b`. The underlaying target is allowed to be unavailable up to a
    * duration of `t`. After successfully reading data, an attempt is made to
    * deduce the origin of the justly read data. This origin address is placed
    * into `p`.
    *
    * Omitting `t` or providing a negative value for `t` will block until the
    * underlaying target is available for reading.
    */
   virtual Expected<size_t> read(std::vector<uint8_t>& b, std::string& p, const std::chrono::milliseconds& t) = 0;
   virtual Expected<size_t> read(std::vector<uint8_t>& b, std::string& p) = 0;
};

struct WriterTo
{
   /**
    * write attempts to write the given buffer `b` to the given address `p` and
    * allows the underlaying transport to be unavailable for a duration of `t`.
    *
    * Omitting `t` or providing a negative value for `t` will block until the
    * underlaying target is available for writing.
    */
   virtual Expected<size_t> write(const std::vector<uint8_t>& b, const std::string& p, const std::chrono::milliseconds& t) = 0;
   virtual Expected<size_t> write(const std::vector<uint8_t>& b, const std::string& p) = 0;
};

/**
 * UDPConnection can be obtained from either `listen_udp` (which'll produce a
 * bound UDP connection) or `dial_udp` (which'll provide a connectionless
 * UDP).
 *
 * The UDP specific read and write mechanisms are defined `ReaderFrom` and
 * `WriterTo`.
 */
struct UDPConnection
   : Connection
   , ReaderFrom
   , WriterTo
{
   const std::string unknown_addr = "?";

   // Let us help the compiler resolve the location of all these overloaded
   // methods.

   /**
    * read on a dialing connection is forbidden, unless a call to `bind` ended
    * in a success.
    */
   using Reader::read;
   using ReaderFrom::read;
   /**
    * write without a remote address on a listening connection is forbidden
    */
   using Writer::write;
   using WriterTo::write;
};

/**
 * listen_udp creates a new UDP connection which'll listen on the given
 * address.
 */
std::shared_ptr<UDPConnection> listen_udp(const std::string& address);

/**
 * dial_udp creates a new UDP connection which defaults it's reads from and
 * writes to the provided address.
 */
std::shared_ptr<UDPConnection> dial_udp(const std::string& address);

#endif
