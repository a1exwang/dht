#include <albert/common/memory.hpp>

#include <backward.hpp>
#include <boost/stacktrace.hpp>

#include <albert/bt/peer_connection.hpp>

namespace boost { namespace stacktrace {

template <class CharT, class TraitsT, class Allocator>
std::basic_ostream<CharT, TraitsT>& do_stream_st(std::basic_ostream<CharT, TraitsT>& os, const basic_stacktrace<Allocator>& bt) {
  const std::streamsize w = os.width();
  const std::size_t frames = bt.size();
  for (std::size_t i = 0; i < frames; ++i) {
    os.width(2);
    os << i;
    os.width(w);
    os << "# ";
    os << bt[i].name();
    os << '\n';
  }

  return os;
}

}}  // namespace boost::stacktrace

namespace albert::common {

/*std::vector<backward::ResolvedTrace>*/ void get_traces(bool verbose = true) {
//  backward::StackTrace st;
//  st.load_here(8);

//  backward::TraceResolver tr;
//  tr.load_stacktrace(st);
//  std::vector<backward::ResolvedTrace> ret;
  // skip current function
  std::stringstream ss;
  auto t0 = std::chrono::high_resolution_clock::now();
  boost::stacktrace::stacktrace st;
  for (size_t i = 1; i < st.size(); ++i) {
//    backward::ResolvedTrace trace = tr.resolve(st[i]);
    if (verbose) {
      ss << "sp: stacktrace " << i
//         << " " << trace.object_filename
//         << " " << trace.object_function
//         << " " << trace.addr
//         << " " << trace.source.filename
//         << ":" << trace.source.line
//         << "," << trace.source.col;
          << " " <<  st[i].address() << " " << st[i].name();
      if (i != st.size() - 1) {
        ss << std::endl;
      }
    }
//    ret.push_back(trace);
  }

  auto t1 = std::chrono::high_resolution_clock::now();
  LOG(info) << "Resolve time " << std::chrono::duration<float>(t1 - t0).count() << "s";
  LOG(info) << ss.str();
//  return ret;
}


template <typename T>
std::set<sp<T>*> sp<T>::instances;
template <typename T>
std::mutex sp<T>::instances_lock;
template <typename T>
std::atomic<size_t> sp<T>::instance_count = 0;

template <typename T>
sp<T>::sp() :impl_(nullptr) {
  {
    std::unique_lock _(instances_lock);
    instances.insert(this);
  }
  instance_count++;
}

// p = make_shared<T>(...)
template <typename T>
sp<T>::sp(T *p) :impl_(p) {
  {
    std::unique_lock _(instances_lock);
    instances.insert(this);
  }
  LOG(info) << "sp: ctor1 " << impl_.get() << " " << impl_.use_count() << " " << p << " " << 0;
  get_traces();
}

// p = wp.lock()
template <typename T>
sp<T>::sp(std::shared_ptr<T> impl) {
  {
    std::unique_lock _(instances_lock);
    instances.insert(this);
  }
  LOG(info) << "sp: ctor2 " << impl_.get() << " " << impl_.use_count() << " " << impl.get() << " " << impl.use_count();
  get_traces();

  impl_ = impl;
}

// auto p1 = p2
template <typename T>
sp<T>::sp(const sp<T> &rhs) {
  {
    std::unique_lock _(instances_lock);
    instances.insert(this);
  }
  LOG(info) << "sp: ctor3 " << impl_.get() << " " << impl_.use_count() << " " << rhs.impl_.get() << " " << rhs.impl_.use_count();
  get_traces();

  impl_ = rhs.impl_;
}

// p1 = p2
template <typename T>
sp<T> &sp<T>::operator=(const sp<T> &rhs) {
  LOG(info) << "sp: = " << impl_.get() << " " << impl_.use_count() << " " << rhs.impl_.get() << " " << rhs.impl_.use_count();
  get_traces();

  if (this != &rhs) {
    impl_ = rhs.impl_;
  }
  return *this;
}

template <typename T>
sp<T>::~sp() {
  {
    std::unique_lock _(instances_lock);
    instances.erase(this);
    LOG(info) << "sp: instances " << typeid(T).name() << " " << instances.size();
  }
  LOG(info) << "sp: destructor " << impl_.get() << " " << impl_.use_count();
  get_traces();
}


template class sp<albert::bt::peer::PeerConnection>;
template class sp<const albert::bt::peer::PeerConnection>;


}

