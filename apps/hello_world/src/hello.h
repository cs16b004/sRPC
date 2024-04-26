#pragma once

#include "rrr.hpp"

#include <errno.h>


class HelloWorldService: public rrr::Service {
public:
    enum {
        HELLOW = 0x10000001,
        ADD_SHORT = 0x10000002,
    };
    int __reg_to__(rrr::Server* svr) {
        int ret = 0;
        if ((ret = svr->reg(HELLOW, this, &HelloWorldService::__hellow__wrapper__)) != 0) {
            goto err;
        }
        if ((ret = svr->reg(ADD_SHORT, this, &HelloWorldService::__add_short__wrapper__)) != 0) {
            goto err;
        }
        return 0;
    err:
        svr->unreg(HELLOW);
        svr->unreg(ADD_SHORT);
        return ret;
    }
    // these RPC handler functions need to be implemented by user
    // for 'raw' handlers, remember to reply req, delete req, and sconn->release(); use sconn->run_async for heavy job
    virtual void hellow(const std::string& in, std::string* out) = 0;
    virtual void add_short(const rrr::i64& a, rrr::i32* out) = 0;
private:
    void __hellow__wrapper__(rrr::Request<rrr::TransportMarshal>* req, rrr::ServerConnection* sconn) {
        std::string in_0;
        req->m >> in_0;
        std::string out_0;
        this->hellow(in_0, &out_0);
        sconn->begin_reply(req);
        *sconn << out_0;
        sconn->end_reply();
    }
    void __add_short__wrapper__(rrr::Request<rrr::TransportMarshal>* req, rrr::ServerConnection* sconn) {
        rrr::i64 in_0;
        req->m >> in_0;
        rrr::i32 out_0;
        this->add_short(in_0, &out_0);
        sconn->begin_reply(req);
        *sconn << out_0;
        sconn->end_reply();
    }
};

class HelloWorldProxy {
protected:
    rrr::Client* __cl__;
public:
    HelloWorldProxy(rrr::Client* cl): __cl__(cl) { }
    rrr::Future* async_hellow(const std::string& in, const rrr::FutureAttr& __fu_attr__ = rrr::FutureAttr()) {
        rrr::Future* __fu__ = __cl__->begin_request(HelloWorldService::HELLOW, __fu_attr__);
        if (__fu__ != nullptr) {
            *__cl__ << in;
        }
        __cl__->end_request();
        return __fu__;
    }
    rrr::i32 hellow(const std::string& in, std::string* out) {
        rrr::Future* __fu__ = this->async_hellow(in);
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
        rrr::Future* __fu__ = __cl__->begin_request(HelloWorldService::ADD_SHORT, __fu_attr__);
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



