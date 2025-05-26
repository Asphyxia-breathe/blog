#include <drogon/drogon.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/HttpAppFramework.h>



int main() {
    // 设置日志
    drogon::app()
        .setLogPath("./")
        .setLogLevel(trantor::Logger::kDebug);

    // 加载配置文件
    drogon::app().loadConfigFile("config.json");
    // 设置监听地址
    drogon::app().addListener("0.0.0.0", 4444);

    // 注册跨域预处理逻辑
    drogon::app().registerPreHandlingAdvice(
    [](const drogon::HttpRequestPtr &req,
       drogon::AdviceCallback &&acb,
       drogon::AdviceChainCallback &&ccb) {
        if (req->method() == drogon::HttpMethod::Options) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k200OK);  // 明确返回 200 状态码
            resp->addHeader("Access-Control-Allow-Origin", "*");
            resp->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS, PUT, DELETE");
            resp->addHeader("Access-Control-Allow-Headers", "Authorization, Content-Type");
            resp->addHeader("Access-Control-Max-Age", "86400");
            resp->setContentTypeCode(drogon::CT_TEXT_PLAIN);  // 明确 Content-Type
            resp->setBody("");  // 非空 body，避免被压缩丢失 header
            acb(resp);
        } else {
            ccb();
        }
    }
);


    // 启动服务
    drogon::app().run();
    return 0;
}
