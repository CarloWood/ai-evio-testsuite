#include "debug.h"
#include <ctime>
#include <cstdlib>
#include <cstdio>
#include <thread>
#include <chrono>
#include <array>
#include <deque>
#include <string>
#include <sstream>
#include <boost/asio.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/bind.hpp>

namespace test_html_server {

using boost::asio::ip::tcp;

char const* const reply =
    "HTTP/1.1 200 OK\r\n"
    "Keep-Alive: timeout=10 max=400\r\n"
    "Content-Length: %lu\r\n"
    "Content-Type: text/html\r\n"
    "X-Connection: %d\r\n"
    "X-Request: %lu\r\n"
    "X-Reply: %d\r\n"
    "\r\n"
    "<html><body>%s</body></html>\n";

char const* const reading_prefix = "    < ";
char const* const writing_prefix = "    > ";

class parser
{
 public:
  parser(std::string const& str) : m_str(str) { reset(); }
  void reset() { m_match = false; m_ptr = m_str.begin(); }

  operator bool() const { return m_match; }
  void feed(char c);

 private:
  std::string m_str;
  bool m_match;
  std::string::iterator m_ptr;
};

void parser::feed(char c)
{
  if (c != *m_ptr)
  {
    reset();
  }
  else
  {
    m_match = ++m_ptr == m_str.end();
  }
}

class header
{
 public:
  enum state_type { s_begin, s_key, s_colon, s_value, s_carriage_return, s_matched };

  header() { reset(); }
  void reset() { m_key.clear(); m_value.clear(); m_state = s_begin; m_error = false; }

  operator bool() const { return m_state == s_matched; }
  void feed(char c);

  std::string const& key() const { return m_key; }
  std::string const& value() const { return m_value; }

 private:
  std::string m_key;
  std::string m_value;
  state_type m_state;
  bool m_error;
};

void header::feed(char c)
{
  if (m_state == s_matched)
    reset();

  switch (m_state)
  {
    case s_key:
      if (c == ':')
      {
        m_state = s_colon;
        break;
      }
      // fall-through
    case s_begin:
      m_key += c;
      m_state = s_key;
      break;
    case s_colon:
      if (c == ' ')
        m_state = s_value;
      else
        m_error = true;
      break;
    case s_value:
      if (c == '\r')
        m_state = s_carriage_return;
      else
        m_value += c;
      break;
    case s_carriage_return:
      if (c == '\n')
      {
        if (!m_error)
        {
          m_state = s_matched;
          return;
        }
        else
          reset();
      }
      else
        m_error = true;
      break;
    case s_matched:
      break;
  }
  if (c == '\n')
    reset();
}

class tcp_connection;

class Reply
{
 public:
  Reply(boost::asio::io_service& io_service, boost::shared_ptr<tcp_connection> const& connection, char const* s, size_t l) :
      m_timer(new boost::asio::deadline_timer(io_service)), m_str(s, l), m_sleep(0), m_must_close(false), m_has_request(false), m_connection(connection) { }
  void set_sleeping(unsigned long sleep);
  bool is_sleeping() const { return m_sleep != 0; }
  void set_must_close() { m_must_close = true; }
  void set_request() { m_has_request = true; }
  bool must_close() const { return m_must_close; }
  bool has_request() const { return m_has_request; }
  void wakeup() { if (is_sleeping()) { m_sleep = 0; m_timer.reset(); } }
  std::string const& str() const { return m_str; }
  void timed_out(boost::system::error_code const& error);

 private:
  boost::shared_ptr<boost::asio::deadline_timer> m_timer;
  std::string m_str;
  unsigned long m_sleep;
  bool m_must_close;
  bool m_has_request;
  boost::shared_ptr<tcp_connection> m_connection;
};

void Reply::set_sleeping(unsigned long sleep)
{
  if (sleep > 0)
  {
    m_sleep = sleep;
    m_timer->expires_from_now(boost::posix_time::millisec(m_sleep));
    m_timer->async_wait(boost::bind(&Reply::timed_out, this, boost::asio::placeholders::error));
  }
  else
  {
    wakeup();
  }
}

class tcp_connection : public boost::enable_shared_from_this<tcp_connection>
{
 public:
  typedef boost::shared_ptr<tcp_connection> pointer;

  static pointer create(boost::asio::io_service& io_service, int instance)
  {
    return pointer(new tcp_connection(io_service, instance));
  }

  tcp::socket& socket()
  {
    return m_socket;
  }

  void start()
  {
    Dout(dc::notice, prefix() << "Accepted a new client.");
    m_socket.async_read_some(boost::asio::buffer(m_buffer),
        boost::bind(&tcp_connection::handle_read, shared_from_this(),
          boost::asio::placeholders::error,
          boost::asio::placeholders::bytes_transferred));
  }

 private:
  tcp_connection(boost::asio::io_service& io_service, int instance) :
    m_io_service(io_service), m_instance(instance), m_reply(0), m_closed(false),
    m_socket(io_service), m_eom("\r\n\r\n"), m_sleep(0), m_must_close(false), m_request(0) { }

  void handle_read(const boost::system::error_code& e, std::size_t bytes_transferred)
  {
    if (!e)
    {
      Dout(dc::notice, prefix() << "Read " << bytes_transferred << " bytes:");

      bool new_message = true;
      std::ostringstream oss;
      for (char const* p = m_buffer.data(); p < m_buffer.data() + bytes_transferred; ++p)
      {
        if (new_message)
        {
          oss << reading_prefix;
          new_message = false;
        }
        m_eom.feed(*p);
        m_header.feed(*p);
        if (*p == '\n')
        {
          oss << "\\n";
          Dout(dc::notice, oss.str());
          oss = std::ostringstream();
          if (!m_eom)
          {
            oss << reading_prefix;
          }
          else
          {
            new_message = true;
          }
        }
        else if (*p == '\r')
        {
          oss << "\\r";
        }
        else
        {
          oss << *p;
        }
        if (m_eom)
        {
          m_eom.reset();
          m_header.reset();
          // Send reply every time we received the sequence "\r\n\r\n".
          queue_reply();
          m_sleep = 0;
          m_must_close = false;
          m_request = 0;
        }
        else if (m_header)
        {
          if (m_header.key() == "X-Sleep")
            m_sleep = strtoul(m_header.value().data(), NULL, 10);
          else if (m_header.key() == "X-must-close")
            m_must_close = true;
          else if (m_header.key() == "X-Request")
            m_request = strtoul(m_header.value().data(), NULL, 10);
        }
      }

      // Always receive more data (we're pipelining).
      m_socket.async_read_some(boost::asio::buffer(m_buffer),
          boost::bind(&tcp_connection::handle_read, shared_from_this(),
            boost::asio::placeholders::error,
            boost::asio::placeholders::bytes_transferred));
    }
    else if (e != boost::asio::error::operation_aborted)
    {
      Dout(dc::notice, prefix() << "Error " << e << ". Closing connection.");
      m_reply_queue.clear();
      m_socket.close();
      m_closed = true;
    }
  }

  void handle_write(const boost::system::error_code& e, size_t CWDEBUG_ONLY(bytes_transferred))
  {
    if (!e)
      Dout(dc::notice, prefix() << "Wrote " << bytes_transferred << " bytes.");
    else
      Dout(dc::notice, prefix() << "Error " << e << " writing data.");
  }

  void queue_reply()
  {
    char body[256];
    int size = std::snprintf(body, sizeof(body), "Reply %d on connection %d for request #%lu", ++m_reply, m_instance, m_request);
    ASSERT(size < ssizeof(body));
    char buf[512];
    size = std::snprintf(buf, sizeof(buf), reply, strlen(body) + 27, m_instance, m_request, m_reply, body);
    ASSERT(size < ssizeof(buf));
    std::string reply_formatted(buf, size);
    m_reply_queue.push_back(Reply(m_io_service, shared_from_this(), buf, size));
    Reply* rp = &m_reply_queue.back();
    if (m_request)
    {
      rp->set_request();
      m_request = 0;
    }
    if (m_sleep)
      rp->set_sleeping(m_sleep);
    if (m_must_close)
      rp->set_must_close();
    process_replies();
  }

 public:
  void process_replies()
  {
    if (m_closed)
      return;
    if (m_reply_queue.empty())
    {
      Dout(dc::notice, prefix() << "process_replies(): nothing to write.");
      return;
    }
    while (!m_reply_queue.empty())
    {
      Reply& r = m_reply_queue.front();
      if (r.is_sleeping())
      {
        return;
      }
      if (!r.has_request() && r.must_close())
      {
        Dout(dc::notice, prefix() << "process_replies(): closing connection.");
        m_reply_queue.clear();
        m_socket.close();
        m_closed = true;
        return;
      }
      Dout(dc::notice, prefix() << "process_replies(): writing data:");
      bool new_line = true;
      std::ostringstream oss;
      for (std::string::const_iterator p = r.str().begin(); p < r.str().end(); ++p)
      {
        if (new_line)
        {
          oss << writing_prefix;
          new_line = false;
        }
        if (*p == '\r')
        {
          oss << "\\r";
        }
        else if (*p == '\n')
        {
          oss << "\\n";
          new_line = true;
          Dout(dc::notice, oss.str());
          oss = std::ostringstream();
        }
        else
        {
          oss << *p;
        }
      }
      if (!new_line)
      {
        Dout(dc::notice, oss.str() << "<NO-NEW-LINE>");
      }
      boost::asio::async_write(m_socket, boost::asio::buffer(r.str()),
          boost::bind(&tcp_connection::handle_write, shared_from_this(),
            boost::asio::placeholders::error,
            boost::asio::placeholders::bytes_transferred));
      m_reply_queue.pop_front();
      if (r.must_close())
      {
        Dout(dc::notice, prefix() << "process_replies(): closing connection.");
        m_reply_queue.clear();
        m_socket.close();
        m_closed = true;
        return;
      }
    }
  }

  std::string prefix() const
  {
    struct timeval tv;
    time_t nowtime;
    struct tm *nowtm;
    char tmbuf[64], buf[80];

    gettimeofday(&tv, NULL);
    nowtime = tv.tv_sec;
    nowtm = localtime(&nowtime);
    strftime(tmbuf, sizeof(tmbuf), "%Y-%m-%d %H:%M:%S", nowtm);
    snprintf(buf, sizeof(buf), "%s.%06lu: #%d: ", tmbuf, tv.tv_usec, m_instance);
    return buf;
  }

 private:
  boost::asio::io_service& m_io_service;
  int m_instance;
  int m_reply;
  bool m_closed;
  tcp::socket m_socket;
  boost::array<char, 8192> m_buffer;
  parser m_eom;
  header m_header;
  unsigned long m_sleep;
  bool m_must_close;
  unsigned long m_request;
  std::deque<Reply> m_reply_queue;
};

void Reply::timed_out(boost::system::error_code const& error)
{
  if (!error)
  {
    m_sleep = 0;        // Not sleeping anymore.
    m_connection->process_replies();
  }
}

class tcp_server
{
 public:
  tcp_server(boost::asio::io_service& io_service) : m_io_service(io_service), m_acceptor(io_service, tcp::endpoint(tcp::v4(), 9001)), m_count(0)
  {
    m_acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
    start();
  }

  void start()
  {
    Dout(dc::notice, "Listening on port 9001...");
    start_accept();
  }

  void stop()
  {
    m_acceptor.close();
  }

 private:
  void start_accept()
  {
    tcp_connection::pointer new_connection = tcp_connection::create(m_io_service, ++m_count);
    m_acceptor.async_accept(new_connection->socket(), boost::bind(&tcp_server::handle_accept, this, new_connection, boost::asio::placeholders::error));
  }

  void handle_accept(tcp_connection::pointer new_connection, boost::system::error_code const& error)
  {
    if (!error)
    {
      new_connection->start();
    }
    start_accept();
  }

  boost::asio::io_service& m_io_service;
  tcp::acceptor m_acceptor;
  int m_count;
};

} // namespace test_html_server

// Create a pipeline capable html server that listens on localhost:9001
// and accepts requests as returned by generate_request(request, sleep).
//
// Replies to each HTML request is given after sleep milliseconds.

class HtmlPipeLineServerFixture : public testing::Test
{
 private:
  std::thread m_thread;
  boost::asio::io_service m_io_service;
  test_html_server::tcp_server m_server;

 protected:
  HtmlPipeLineServerFixture() : m_server(m_io_service) { }

  void SetUp()
  {
#ifdef CWDEBUG
    Dout(dc::notice, "v HtmlPipeLineServerFixture::SetUp()");
    debug::Mark setup;
#endif
    std::thread thr([this](){
          Debug(debug::init_thread("HtmlServer"));
          Dout(dc::notice, "Thread started.");
          m_io_service.reset();
          m_server.start();
          Dout(dc::notice|flush_cf, "Calling m_io_service.run()...");
          m_io_service.run();
          Dout(dc::notice|flush_cf, "Returned from m_io_service.run().");
        });
    m_thread = std::move(thr);
  }

  void TearDown()
  {
    Dout(dc::notice, "v HtmlPipeLineServerFixture::TearDown()");
    m_io_service.stop();
    m_server.stop();
    Dout(dc::notice, "Calling m_thread.join()");
    m_thread.join();
    Dout(dc::notice|flush_cf, "Returned from m_thread.join()");
  }

  std::string generate_request(int request, int sleep)
  {
    std::ostringstream oss;
    oss << "GET / HTTP/1.1\r\n"
           "Host: localhost:9001\r\n"
           "Accept: */*\r\n"
           "X-Request: " << request << "\r\n"
           "X-Sleep: " << sleep << "\r\n"
           "\r\n";
    return oss.str();
  }

  std::string request_close(int sleep)
  {
    std::ostringstream oss;
    oss << "GET / HTTP/1.1\r\n"
           "X-Sleep: " << sleep << "\r\n"
           "X-must-close: yes\r\n"
           "\r\n";
    return oss.str();
  }

  std::string generate_request_with_close(int request, int sleep)
  {
    std::ostringstream oss;
    oss << "GET / HTTP/1.1\r\n"
           "Host: localhost:9001\r\n"
           "Accept: */*\r\n"
           "X-Request: " << request << "\r\n"
           "X-Sleep: " << sleep << "\r\n"
           "X-must-close: yes\r\n"
           "\r\n";
    return oss.str();
  }
};
