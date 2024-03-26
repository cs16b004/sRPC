#pragma once

#include "rrr.hpp"

#include <errno.h>


class CounterService: public rrr::Service {
public:
    enum {
        ADD = 0x10000001,
        ADD_LONG = 0x10000002,
        ADD_BENCH = 0x10000003,
        ADD_SHORT = 0x10000004,
    };
    int __reg_to__(rrr::Server* svr) {
        int ret = 0;
        if ((ret = svr->reg(ADD, this, &CounterService::__add__wrapper__)) != 0) {
            goto err;
        }
        if ((ret = svr->reg(ADD_LONG, this, &CounterService::__add_long__wrapper__)) != 0) {
            goto err;
        }
        if ((ret = svr->reg(ADD_BENCH, this, &CounterService::__add_bench__wrapper__)) != 0) {
            goto err;
        }
        if ((ret = svr->reg(ADD_SHORT, this, &CounterService::__add_short__wrapper__)) != 0) {
            goto err;
        }
        return 0;
    err:
        svr->unreg(ADD);
        svr->unreg(ADD_LONG);
        svr->unreg(ADD_BENCH);
        svr->unreg(ADD_SHORT);
        return ret;
    }
    // these RPC handler functions need to be implemented by user
    // for 'raw' handlers, remember to reply req, delete req, and sconn->release(); use sconn->run_async for heavy job
    virtual void add() = 0;
    virtual void add_long(const rrr::i32& a, const rrr::i32& b, const rrr::i32& c, const rrr::i64& d, const rrr::i64& e, const std::vector<rrr::i64>& input, rrr::i32* out, std::vector<rrr::i64>* output) = 0;
    virtual void add_bench(const std::string& in, std::string* out) = 0;
    virtual void add_short(const rrr::i64& a, rrr::i32* out) = 0;
private:
    void __add__wrapper__(rrr::Request<rrr::TransportMarshal>* req, rrr::ServerConnection* sconn) {
        this->add();
        sconn->begin_reply(req);
        sconn->end_reply();
        delete req;
        sconn->release();
    }
    void __add_long__wrapper__(rrr::Request<rrr::TransportMarshal>* req, rrr::ServerConnection* sconn) {
        rrr::i32 in_0;
        req->m >> in_0;
        rrr::i32 in_1;
        req->m >> in_1;
        rrr::i32 in_2;
        req->m >> in_2;
        rrr::i64 in_3;
        req->m >> in_3;
        rrr::i64 in_4;
        req->m >> in_4;
        std::vector<rrr::i64> in_5;
        req->m >> in_5;
        rrr::i32 out_0;
        std::vector<rrr::i64> out_1;
        this->add_long(in_0, in_1, in_2, in_3, in_4, in_5, &out_0, &out_1);
        sconn->begin_reply(req);
        *sconn << out_0;
        *sconn << out_1;
        sconn->end_reply();
        delete req;
        sconn->release();
    }
    void __add_bench__wrapper__(rrr::Request<rrr::TransportMarshal>* req, rrr::ServerConnection* sconn) {
        std::string in_0;
        req->m >> in_0;
        std::string out_0;
        this->add_bench(in_0, &out_0);
        sconn->begin_reply(req);
        *sconn << out_0;
        sconn->end_reply();
        delete req;
        sconn->release();
    }
    void __add_short__wrapper__(rrr::Request<rrr::TransportMarshal>* req, rrr::ServerConnection* sconn) {
        rrr::i64 in_0;
        req->m >> in_0;
        rrr::i32 out_0;
        this->add_short(in_0, &out_0);
        sconn->begin_reply(req);
        *sconn << out_0;
        sconn->end_reply();
        delete req;
        sconn->release();
    }
};

class CounterProxy {
protected:
    rrr::Client* __cl__;
public:
    CounterProxy(rrr::Client* cl): __cl__(cl) { }
    rrr::Future* async_add(const rrr::FutureAttr& __fu_attr__ = rrr::FutureAttr()) {
        rrr::Future* __fu__ = __cl__->begin_request(CounterService::ADD, __fu_attr__);
        __cl__->end_request();
        return __fu__;
    }
    rrr::i32 add() {
        rrr::Future* __fu__ = this->async_add();
        if (__fu__ == nullptr) {
            return ENOTCONN;
        }
        rrr::i32 __ret__ = __fu__->get_error_code();
        __fu__->release();
        return __ret__;
    }
    rrr::Future* async_add_long(const rrr::i32& a, const rrr::i32& b, const rrr::i32& c, const rrr::i64& d, const rrr::i64& e, const std::vector<rrr::i64>& input, const rrr::FutureAttr& __fu_attr__ = rrr::FutureAttr()) {
        rrr::Future* __fu__ = __cl__->begin_request(CounterService::ADD_LONG, __fu_attr__);
        if (__fu__ != nullptr) {
            *__cl__ << a;
            *__cl__ << b;
            *__cl__ << c;
            *__cl__ << d;
            *__cl__ << e;
            *__cl__ << input;
        }
        __cl__->end_request();
        return __fu__;
    }
    rrr::i32 add_long(const rrr::i32& a, const rrr::i32& b, const rrr::i32& c, const rrr::i64& d, const rrr::i64& e, const std::vector<rrr::i64>& input, rrr::i32* out, std::vector<rrr::i64>* output) {
        rrr::Future* __fu__ = this->async_add_long(a, b, c, d, e, input);
        if (__fu__ == nullptr) {
            return ENOTCONN;
        }
        rrr::i32 __ret__ = __fu__->get_error_code();
        if (__ret__ == 0) {
            __fu__->get_reply() >> *out;
            __fu__->get_reply() >> *output;
        }
        __fu__->release();
        return __ret__;
    }
    rrr::Future* async_add_bench(const std::string& in, const rrr::FutureAttr& __fu_attr__ = rrr::FutureAttr()) {
        rrr::Future* __fu__ = __cl__->begin_request(CounterService::ADD_BENCH, __fu_attr__);
        if (__fu__ != nullptr) {
            *__cl__ << in;
        }
        __cl__->end_request();
        return __fu__;
    }
    rrr::i32 add_bench(const std::string& in, std::string* out) {
        rrr::Future* __fu__ = this->async_add_bench(in);
        if (__fu__ == nullptr) {
            return ENOTCONN;
        }
        rrr::i32 __ret__ = __fu__->get_error_code();
        if (__ret__ == 0) {
            __fu__->get_reply() >> *out;
        }
        __fu__->release();
        return __ret__;
    }
    rrr::Future* async_add_short(const rrr::i64& a, const rrr::FutureAttr& __fu_attr__ = rrr::FutureAttr()) {
        rrr::Future* __fu__ = __cl__->begin_request(CounterService::ADD_SHORT, __fu_attr__);
        if (__fu__ != nullptr) {
            *__cl__ << a;
        }
        __cl__->end_request();
        return __fu__;
    }
    rrr::i32 add_short(const rrr::i64& a, rrr::i32* out) {
        rrr::Future* __fu__ = this->async_add_short(a);
        if (__fu__ == nullptr) {
            return ENOTCONN;
        }
        rrr::i32 __ret__ = __fu__->get_error_code();
        if (__ret__ == 0) {
            __fu__->get_reply() >> *out;
        }
        __fu__->release();
        return __ret__;
    }
};



