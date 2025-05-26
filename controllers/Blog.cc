#include "Blog.h"
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/orm/DbClient.h>
#include <json/json.h>
#include <drogon/utils/Utilities.h> 
#include <jwt-cpp/jwt.h>
#include <algorithm>
#include <cctype>
#include <codecvt>
#include <locale>
#include <vector>
using namespace drogon;
using namespace drogon::orm;
#include <cstdio>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <fstream>
#include <string>
#include <regex>
#include <nlohmann/json.hpp>
#include "base64.h"
#include <unistd.h>

using json = nlohmann::json;
std::string join(const std::vector<std::string>& vec, const std::string& delimiter) {
    std::string result;
    for (size_t i = 0; i < vec.size(); ++i) {
        result += vec[i];
        if (i != vec.size() - 1) {
            result += delimiter;
        }
    }
    return result;
}
std::wstring utf8ToWstring(const std::string& str) {
    std::wstring_convert<std::codecvt_utf8<wchar_t>> myconv;
    return myconv.from_bytes(str);
}

std::string wstringToUtf8(const std::wstring& wstr) {
    std::wstring_convert<std::codecvt_utf8<wchar_t>> myconv;
    return myconv.to_bytes(wstr);
}
std::vector<std::string> SensitiveWordFilter::detectAllSensitiveWords(const std::string &text) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::wstring wtext = utf8ToWstring(text);
    std::vector<std::string> hits;

    for (size_t i = 0; i < wtext.length(); ++i) {
        auto node = root_;
        std::wstring matched;

        for (size_t j = i; j < wtext.length(); ++j) {
            wchar_t wc = wtext[j];
            if (isSymbol(wc)) continue;

            wchar_t lower = std::towlower(wc);
            if (!node->wchildren.count(lower)) break;

            matched += wc;
            node = node->wchildren[lower];

            if (node->isEnd) {
                hits.push_back(wstringToUtf8(matched));
                break;  // 防止重复计数同一位置
            }
        }
    }

    return hits;
}



std::string runPythonEmotionDetection(const std::string &text) {
    std::string encoded = base64_encode(reinterpret_cast<const unsigned char*>(text.c_str()), text.length());
    std::string command = "python3 /home/feishu/content/llm_api.py emotion --text-base64 \"" + encoded + "\"";
    LOG_INFO<<"执行指令"<<command;
    int status = system(command.c_str());
    if (status != 0) {
        LOG_ERROR << "Emotion analysis script failed with status: " << status;
        return "未知";
    }

    std::ifstream file("/home/feishu/content/emotion_result.json");
    if (!file.is_open()) {
        LOG_ERROR << "Failed to open result file";
        return "未知";
    }

    json obj;
    try {
        file >> obj;
    } catch (const std::exception& e) {
        LOG_ERROR << "Failed to parse JSON: " << e.what();
        return "未知";
    }

    return obj.contains("type") && obj["type"].is_string() ? obj["type"] : "未知";
}




std::string runPythonAnimalDetection(const std::string &imageUrl) {
    std::string command = "python3 /home/feishu/content/llm_api.py animal --image-url \"" + imageUrl + "\"";
    LOG_INFO<<"执行指令"<<command;
    int status = system(command.c_str());
    if (status != 0) {
        LOG_ERROR << "Animal detection script failed with status: " << status;
        return "{}";  // 返回空json字符串表示失败
    }

    std::ifstream file("/home/feishu/content/animal_result.json");
    if (!file.is_open()) {
        LOG_ERROR<< "Failed to open result file: /home/feishu/content/animal_result.json";
        return "{}";
    }

    std::string resultJsonStr((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());

    if (resultJsonStr.empty()) {
        LOG_ERROR << "Result file is empty";
        return "{}";
    }

    return resultJsonStr;
}



std::vector<std::string> extractImageUrls(const std::string& content) {
    std::string cleanContent = std::regex_replace(content, std::regex(R"(\\")"), "\"");

    std::vector<std::string> imageUrls;
    std::regex imgRegex(R"(<img\s+[^>]*src\s*=\s*["']?([^"' >]+)["']?)");
    std::smatch match;
    std::string::const_iterator searchStart(cleanContent.cbegin());

    while (std::regex_search(searchStart, cleanContent.cend(), match, imgRegex)) {
        imageUrls.push_back(match[1]);
        searchStart = match.suffix().first;
    }


    return imageUrls;
}

bool checkTokenValid(const HttpRequestPtr &req, const std::function<void(const HttpResponsePtr &)> &callback)
{
    if (req->method() == drogon::HttpMethod::Options) {
        return true;  // 预检请求直接放行
    }

    auto token = req->getHeader("Authorization");
     LOG_INFO<<"执行语句前的token:"<<token;
    if (token.empty()) {
        Json::Value result;
        result["code"] = 0;
        
        result["error"] = "未提供token";
        LOG_INFO<<"143行说没提取到";
        auto resp = HttpResponse::newHttpJsonResponse(result);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
        return false;
    }

    auto dbClient = drogon::app().getDbClient();
    auto result = dbClient->execSqlSync(
        "SELECT account FROM tokens WHERE token = ?", token
    );
    LOG_INFO<<"执行语句"<<"SELECT account FROM tokens WHERE token = "<<token;

    if (result.empty()) {
        Json::Value resultJson;
        resultJson["code"] = 0;
        resultJson["error"] = "token无效";
        LOG_INFO<<"160行说没提取到";
        auto resp = HttpResponse::newHttpJsonResponse(resultJson);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
        return false;
    }

    return true;  // token 有效
}


SensitiveWordFilter::SensitiveWordFilter() 
    : root_(std::make_shared<TrieNode>()) {}

void SensitiveWordFilter::loadFromDbSync() {
    auto dbClient = drogon::app().getDbClient();
    auto result = dbClient->execSqlSync("SELECT word FROM sensitive_words");

    std::lock_guard<std::mutex> lock(mutex_);
    root_ = std::make_shared<TrieNode>();
    wordCount_ = 0;

    for (const auto &row : result) {
        insertWord(row["word"].as<std::string>());
        wordCount_++;
    }

    LOG_INFO << "Loaded " << wordCount_ << " sensitive words synchronously";
}

void SensitiveWordFilter::loadFromDb() {
    auto dbClient = app().getDbClient();
    dbClient->execSqlAsync(
        "SELECT word FROM sensitive_words",
        [this](const orm::Result &r) {
            std::lock_guard<std::mutex> lock(mutex_);
            wordCount_ = 0;
            root_ = std::make_shared<TrieNode>();  // 重置树
            
            for (const auto &row : r) {
                insertWord(row["word"].as<std::string>());
                wordCount_++;
            }
            LOG_INFO << "Loaded " << wordCount_ << " sensitive words";
        },
        [](const orm::DrogonDbException &e) {
            LOG_ERROR << "Load sensitive words failed: " << e.base().what();
        });
}

void SensitiveWordFilter::insertWord(const std::string &word) {
    auto node = root_;
    std::wstring wword = utf8ToWstring(word);
    for (wchar_t wc : wword) {
        if (isSymbol(wc)) continue;

        wchar_t lower = std::towlower(wc);
        if (!node->wchildren.count(lower)) {
            node->wchildren[lower] = std::make_shared<TrieNode>();
        }
        node = node->wchildren[lower];
    }
    node->isEnd = true;
}




std::string SensitiveWordFilter::replaceSensitiveWords(const std::string &text, char replaceChar) const {
    std::string result = text;
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (size_t i = 0; i < result.length(); ) {
        auto node = root_;
        size_t matchLength = 0;
        size_t j = i;
        
        while (j < result.length()) {
            char c = result[j];
            if (isSymbol(c)) {
                j++;
                continue;
            }
            
            char lower = std::tolower(c);
            if (!node->wchildren.count(lower)) break;
            
            node = node->wchildren[lower];
            j++;
            matchLength++;
            
            if (node->isEnd) {
                for (size_t k = i; k < i + matchLength; k++) {
                    if (!isSymbol(result[k])) {
                        result[k] = replaceChar;
                    }
                }
                i = j;
                break;
            }
        }
        
        if (matchLength == 0) i++;
    }
    return result;
}

size_t SensitiveWordFilter::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return wordCount_;
}


bool SensitiveWordFilter::isSymbol(wchar_t wc) const {
    return std::iswspace(wc) || std::iswpunct(wc) || std::iswcntrl(wc);
}


SensitiveWordFilter &SensitiveWordFilter::instance() {
    static SensitiveWordFilter instance;
    return instance;
}

void recordSuspiciousContent(
    int64_t userId,
    std::string &hitWord,
    int type

) {
    auto dbClient = drogon::app().getDbClient();

    // 1. 查询用户名
    dbClient->execSqlAsync(
        "SELECT username FROM users WHERE id = ?",
        [=](const drogon::orm::Result &result) {  // 成功回调
            if (result.empty()) {
                LOG_ERROR << "User not found: " << userId;
                return;
            }

            std::string account = result[0]["username"].as<std::string>();

            // 2. 插入异常记录
            Json::Value detailJson;
            if(type==0){
            detailJson["detail"] = "标题包含敏感内容:"+hitWord;
            }
            if(type==1){
            detailJson["detail"] = "正文包含敏感内容:"+hitWord;
            }
            if(type==2){
                 detailJson["detail"] = "内容消极";
            }
            if(type==3){
                 detailJson["detail"] = "图片涉及"+hitWord;
            }

            Json::StreamWriterBuilder writer;
            writer["emitUTF8"] = true;
            writer["indentation"] = "";
            std::string detailStr = Json::writeString(writer, detailJson);

            dbClient->execSqlAsync(
                "INSERT INTO account_abnormal_record "
                "(user_id, account, abnormal_type, abnormal_detail, is_resolved, create_time) "
                "VALUES (?, ?, ?, ?, ?, ?)",
                [](const drogon::orm::Result &r) {  // 成功回调
                    LOG_INFO << "Abnormal record inserted";
                },
                [](const drogon::orm::DrogonDbException &e) {  // 失败回调
                    LOG_ERROR << "Failed to insert record: " << e.base().what();
                },
                userId,
                account,
                "SUSPICIOUS_CONTENT-内容异常",
                detailStr,
                0,
                trantor::Date::now().toDbString()
            );
        },
        [=](const drogon::orm::DrogonDbException &e) {  // 失败回调
            LOG_ERROR << "Failed to query username: " << e.base().what();
        },
        userId  // SQL 参数（绑定到 ?）
    );
}
void Content::exportPdf(const HttpRequestPtr &req,
                         std::function<void(const HttpResponsePtr &)> &&callback,
                         int id)
{
    if (!checkTokenValid(req, callback)) {
        LOG_WARN << "token无效";
        Json::Value result;
        result["code"] = 0;
        result["error"] = "token timed out";
        auto resp = HttpResponse::newHttpJsonResponse(result);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
        return;
    }

    auto dbClient = drogon::app().getDbClient();

    dbClient->execSqlAsync(
        "SELECT title, content, account, created_at, updated_at FROM articles WHERE id = ?",
        [=](const drogon::orm::Result &res) {
            if (res.empty()) {
                Json::Value err;
                err["code"]=0;
                err["message"] = "文章不存在";
                auto resp = HttpResponse::newHttpJsonResponse(err);
                resp->setStatusCode(k404NotFound);
                resp->addHeader("Access-Control-Allow-Origin", "*");
                return callback(resp);
            }

            // 取字段
            std::string title = res[0]["title"].as<std::string>();
            std::string content = res[0]["content"].as<std::string>();
            std::string author = res[0]["account"].as<std::string>();
            std::string created_at = res[0]["created_at"].as<std::string>();
            std::string updated_at = res[0]["updated_at"].as<std::string>();

            // 读取 HTML 模板
            std::string htmlTemplatePath = "./views/blog_pdf.html";
            std::ifstream in(htmlTemplatePath);
            std::stringstream buffer;
            buffer << in.rdbuf();
            std::string html = buffer.str();

            // 替换变量
            auto replace_all = [](std::string &s, const std::string &from, const std::string &to) {
                size_t pos = 0;
                while ((pos = s.find(from, pos)) != std::string::npos) {
                    s.replace(pos, from.length(), to);
                    pos += to.length();
                }
            };

            replace_all(html, "{{title}}", title);
            replace_all(html, "{{author}}", author);
            replace_all(html, "{{created_at}}", created_at);
            replace_all(html, "{{updated_at}}", updated_at);
            replace_all(html, "{{content}}", content);

            // 保存为临时 HTML 文件
            std::string tempHtmlPath = "/tmp/blog_" + std::to_string(id) + ".html";
            std::string tempPdfPath = "/tmp/blog_" + std::to_string(id) + ".pdf";

            std::ofstream out(tempHtmlPath);
            out << html;
            out.close();

            // 调用 wkhtmltopdf
            std::string cmd = "wkhtmltopdf " + tempHtmlPath + " " + tempPdfPath;
            int ret = system(cmd.c_str());
            if (ret != 0) {
                Json::Value err;
                err["code"]=0;
                err["error"] = "PDF 生成失败";
                auto resp = HttpResponse::newHttpJsonResponse(err);
                resp->setStatusCode(k500InternalServerError);
                resp->addHeader("Access-Control-Allow-Origin", "*");
                return callback(resp);
            }

            // 读取 PDF 内容
            std::ifstream pdfFile(tempPdfPath, std::ios::binary);
            std::ostringstream oss;
            oss << pdfFile.rdbuf();

            // 创建响应并返回 PDF
            auto resp = HttpResponse::newHttpResponse();
            resp->setContentTypeCode(CT_APPLICATION_PDF);
            resp->addHeader("Content-Disposition", "attachment; filename=\"blog_" + std::to_string(id) + ".pdf\"");
            resp->addHeader("Access-Control-Allow-Origin", "*");
            resp->setBody(oss.str());

            // 清理临时文件（异步或同步）
            std::remove(tempHtmlPath.c_str());


            callback(resp);
        },
        [=](const drogon::orm::DrogonDbException &e) {
            Json::Value err;
            err["code"]=0;
            err["error"] = std::string("数据库错误: ") + e.base().what();
            auto resp = HttpResponse::newHttpJsonResponse(err);
            resp->addHeader("Access-Control-Allow-Origin", "*");
            resp->setStatusCode(k500InternalServerError);
            callback(resp);
        },
        id);
}


// void Content::exportPdf(const HttpRequestPtr &req,
//                          std::function<void(const HttpResponsePtr &)> &&callback,
//                          int id)
// {
//     if (!checkTokenValid(req, callback)) {
//         LOG_WARN << "token无效";
//         Json::Value result;
//         result["code"] = 0;
//         result["error"] = "token timed out";
//         auto resp = HttpResponse::newHttpJsonResponse(result);
//         resp->addHeader("Access-Control-Allow-Origin", "*");
//         resp->setStatusCode(k401Unauthorized);
//         callback(resp);
//         return;
//     }

//     auto dbClient = drogon::app().getDbClient();

//     dbClient->execSqlAsync(
//         "SELECT title, content, account, created_at, updated_at FROM articles WHERE id = ?",
//         [=](const drogon::orm::Result &res) {
//             if (res.empty()) {
//                 Json::Value err;
//                 err["code"]=0;
//                 err["message"] = "文章不存在";
//                 auto resp = HttpResponse::newHttpJsonResponse(err);
//                 resp->setStatusCode(k404NotFound);
//                 resp->addHeader("Access-Control-Allow-Origin", "*");
//                 return callback(resp);
//             }

//             // 取字段
//             std::string title = res[0]["title"].as<std::string>();
//             std::string content = res[0]["content"].as<std::string>();
//             std::string author = res[0]["account"].as<std::string>();
//             std::string created_at = res[0]["created_at"].as<std::string>();
//             std::string updated_at = res[0]["updated_at"].as<std::string>();

//             // 读取 HTML 模板
//             std::string htmlTemplatePath = "./views/blog_pdf.html";
//             std::ifstream in(htmlTemplatePath);
//             std::stringstream buffer;
//             buffer << in.rdbuf();
//             std::string html = buffer.str();

//             // 替换变量
//             auto replace_all = [](std::string &s, const std::string &from, const std::string &to) {
//                 size_t pos = 0;
//                 while ((pos = s.find(from, pos)) != std::string::npos) {
//                     s.replace(pos, from.length(), to);
//                     pos += to.length();
//                 }
//             };

//             replace_all(html, "{{title}}", title);
//             replace_all(html, "{{author}}", author);
//             replace_all(html, "{{created_at}}", created_at);
//             replace_all(html, "{{updated_at}}", updated_at);
//             replace_all(html, "{{content}}", content);

//             // 保存为临时 HTML 文件
//             std::string tempHtmlPath = "./blog_" + std::to_string(id) + ".html";
//             std::string tempPdfPath = "./blog_" + std::to_string(id) + ".pdf";

//             std::ofstream out(tempHtmlPath);
//             out << html;
//             out.close();

//             // 调用 wkhtmltopdf
//             std::string cmd = "wkhtmltopdf " + tempHtmlPath + " " + tempPdfPath;
//             int ret = system(cmd.c_str());
//             if (ret != 0) {
//                 Json::Value err;
//                 err["code"]=0;
//                 err["error"] = "PDF 生成失败";
//                 auto resp = HttpResponse::newHttpJsonResponse(err);
//                 resp->setStatusCode(k500InternalServerError);
//                 resp->addHeader("Access-Control-Allow-Origin", "*");
//                 return callback(resp);
//             }

//             // 读取 PDF 内容
//             std::string downloadUrl = "./blog_" + std::to_string(id) + ".pdf";
//             // 构造 JSON 响应
//             Json::Value result;
//             result["code"] = 1;
//             result["message"] = downloadUrl;

//             auto resp = HttpResponse::newHttpJsonResponse(result);
//             resp->addHeader("Access-Control-Allow-Origin", "*");
//             callback(resp);
//             // 清理临时文件（异步或同步）
//             std::remove(tempHtmlPath.c_str());


//             callback(resp);
//         },
//         [=](const drogon::orm::DrogonDbException &e) {
//             Json::Value err;
//             err["code"]=0;
//             err["error"] = std::string("数据库错误: ") + e.base().what();
//             auto resp = HttpResponse::newHttpJsonResponse(err);
//             resp->addHeader("Access-Control-Allow-Origin", "*");
//             resp->setStatusCode(k500InternalServerError);
//             callback(resp);
//         },
//         id);
// }











void Content::asyncHandleHttpRequest(const HttpRequestPtr& req,
    std::function<void (const HttpResponsePtr &)> &&callback)
{
    auto resp = HttpResponse::newHttpResponse();
    resp->setBody("<p>Hello, world!</p>");
    resp->setExpiredTime(0);
    callback(resp);
}



void Content::createArticle(const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback)
{
    auto headers = req->getHeaders();
    for (const auto &[key, value] : headers) {
        LOG_INFO << "Header: " << key << " = " << value;
    }
    std::string token = req->getHeader("Authorization");
    LOG_INFO << "createArticle: called";
    LOG_INFO<<"token:"<<token;
    if (!checkTokenValid(req, callback)) {
        LOG_WARN << "token无效";
        Json::Value result;
        result["code"]=0;
        result["error"] = "token timed out";
        auto resp = HttpResponse::newHttpJsonResponse(result);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
        return;  // Token 无效，已返回响应
    }
    
    if (token.empty()) {
        LOG_WARN << "createArticle: Missing token in header";
        Json::Value result;
        result["code"]=0;
        result["error"] = "Missing token";
        auto resp = HttpResponse::newHttpJsonResponse(result);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
        return;
    }
    int userId = -1;
    try {
        auto decoded = jwt::decode(token);
        userId = decoded.get_payload_claim("userId").as_number();
        LOG_INFO << "createArticle: Extracted userId = " << userId;
    } catch (const std::exception &e) {
        LOG_ERROR << "createArticle: Token parse failed: " << e.what();
        Json::Value result;
        result["code"]=0;
        result["error"] = std::string("Token parse failed: ") + e.what();
        auto resp = HttpResponse::newHttpJsonResponse(result);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
        return;
    }
    auto json = req->getJsonObject();
    if(json)
    {
    std::string title = (*json)["title"].asString();
    std::string content= (*json)["content"].asString();
    std::string tagsStr = (*json)["tags"].asString();
    LOG_INFO << "createArticle: Received title: " << title;
    LOG_INFO << "createArticle: Received content: " << content;
    LOG_INFO << "createArticle: Received tags: " << tagsStr;
   
    if (title.empty() || content.empty()) {
        LOG_WARN << "createArticle: Title or content is empty";
        Json::Value result;
        result["code"]=0;
        result["error"] = "Title and content are required";
        auto resp = HttpResponse::newHttpJsonResponse(result);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        resp->setStatusCode(k400BadRequest);
        callback(resp);
        return;
    }

    auto dbClient = drogon::app().getDbClient();
    LOG_INFO << "createArticle: Querying username for userId = " << userId;

    dbClient->execSqlAsync(
        "SELECT username,account FROM users WHERE id = ?",
        [=](const Result &res) {
            if (res.empty()) {
                LOG_WARN << "createArticle: No user found for id = " << userId;
                Json::Value result;
                result["code"]=0;
                result["error"] = "User not found";
                auto resp = HttpResponse::newHttpJsonResponse(result);
                resp->setStatusCode(k404NotFound);
                resp->addHeader("Access-Control-Allow-Origin", "*");
                callback(resp);
                return;
            }

            std::string username = res[0]["username"].as<std::string>();
            std::string account = res[0]["account"].as<std::string>();
            LOG_INFO << "createArticle: Found username = " << username;

            LOG_INFO << "createArticle: Inserting article to database";

            dbClient->execSqlAsync(
                "INSERT INTO articles (author_id, author_name, title, content, tags,account, status) VALUES (?, ?, ?, ?, ?, ?,'draft')",
                [callback](const Result &r) {
                    LOG_INFO << "createArticle: Article insert success, id = " << r.insertId();
                    Json::Value ret;
                    ret["code"]=1;
                    ret["data"]["id"] = std::to_string(r.insertId());
                    ret["message"] = "Article created";
                    auto resp = HttpResponse::newHttpJsonResponse(ret);
                    resp->addHeader("Access-Control-Allow-Origin", "*");
                    resp->setStatusCode(k200OK);
                    resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
                    callback(resp);
                },
                [callback](const DrogonDbException &e) {
                    LOG_ERROR << "createArticle: Article insert DB error: " << e.base().what();
                    Json::Value ret;
                    ret["code"]=0;
                    ret["error"] = std::string("Database error: ") + e.base().what();
                    auto resp = HttpResponse::newHttpJsonResponse(ret);
                    resp->addHeader("Access-Control-Allow-Origin", "*");
                    resp->setStatusCode(k500InternalServerError);
                    callback(resp);
                },
                userId, username, title, content, tagsStr,account
            );
        },
        [callback, userId](const DrogonDbException &e) {
            LOG_ERROR << "createArticle: User query DB error for userId " << userId << ": " << e.base().what();
            Json::Value result;
            result["code"]=0;
            result["error"] = std::string("User query failed: ") + e.base().what();
            auto resp = HttpResponse::newHttpJsonResponse(result);
            resp->addHeader("Access-Control-Allow-Origin", "*");
            resp->setStatusCode(k500InternalServerError);
            callback(resp);
        },
        userId
    );
    }
    else{
        LOG_ERROR << "createArticle: json parse failed: " ;
        Json::Value result;
        result["code"]=0;
        result["error"] = std::string("json parse failed: ") ;
        auto resp = HttpResponse::newHttpJsonResponse(result);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
        return;
    }
    
}


void Content::publishArticle(const HttpRequestPtr &req,
                             std::function<void(const HttpResponsePtr &)> &&callback,
                             int articleId) {
    if (!checkTokenValid(req, callback)) {
        LOG_WARN << "token无效";
        Json::Value result;
        result["code"] = 0;
        result["error"] = "token timed out";
        auto resp = HttpResponse::newHttpJsonResponse(result);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
        return;
    }

    std::string token = req->getHeader("Authorization");
    if (token.empty()) {
        Json::Value result;
        result["code"] = 0;
        result["error"] = "Missing token";
        auto resp = HttpResponse::newHttpJsonResponse(result);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
        return;
    }

    int userId = -1;
    try {
        auto decoded = jwt::decode(token);
        userId = decoded.get_payload_claim("userId").as_number();
    } catch (const std::exception &e) {
        Json::Value result;
        result["code"] = 0;
        result["error"] = std::string("Token parse failed: ") + e.what();
        auto resp = HttpResponse::newHttpJsonResponse(result);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
        return;
    }

    auto dbClient = drogon::app().getDbClient();

    dbClient->execSqlAsync(
        "SELECT author_id, status, title, content, tags FROM articles WHERE id = ?",
        [=](const Result &r) {
            if (r.empty()) {
                Json::Value res;
                res["code"] = 0;
                res["error"] = "Article not found";
                auto resp = HttpResponse::newHttpJsonResponse(res);
                resp->setStatusCode(k404NotFound);
                resp->addHeader("Access-Control-Allow-Origin", "*");
                callback(resp);
                return;
            }

            int authorId = r[0]["author_id"].as<int>();
            std::string status = r[0]["status"].as<std::string>();
            std::string title = r[0]["title"].as<std::string>();
            std::string content = r[0]["content"].as<std::string>();
            std::string tagsStr = r[0]["tags"].as<std::string>();

            if (status == "published") {
                Json::Value res;
                res["code"] = 0;
                res["error"] = "Article already published";
                auto resp = HttpResponse::newHttpJsonResponse(res);
                resp->setStatusCode(k400BadRequest);
                resp->addHeader("Access-Control-Allow-Origin", "*");
                callback(resp);
                return;
            }

            if (authorId != userId) {
                Json::Value res;
                res["code"] = 0;
                res["error"] = "No permission to publish this article";
                auto resp = HttpResponse::newHttpJsonResponse(res);
                resp->setStatusCode(k200OK);
                resp->addHeader("Access-Control-Allow-Origin", "*");
                callback(resp);
                return;
            }

            Json::Value res;
            res["code"] = 1;
            res["message"] = "文章正在审核中，请稍后查看审核结果";
            auto resp = HttpResponse::newHttpJsonResponse(res);
            resp->addHeader("Access-Control-Allow-Origin", "*");
            callback(resp);

            // 后台异步线程执行审核
            std::thread([=]() {
                static std::once_flag loaded;
                std::call_once(loaded, []() {
                    SensitiveWordFilter::instance().loadFromDbSync();
                });

                auto &filter = SensitiveWordFilter::instance();
                std::ostringstream abnormalDetail;
                bool hasProblem = false;

                // 1. 敏感词检测
                std::vector<std::string> titleHits = filter.detectAllSensitiveWords(title);
                std::vector<std::string> contentHits = filter.detectAllSensitiveWords(content);
                std::vector<std::string> tagsHits = filter.detectAllSensitiveWords(tagsStr);

                if (!titleHits.empty()) {
                    hasProblem = true;
                    abnormalDetail << "标题命中敏感词: " << join(titleHits, ", ") << "\n";
                }
                if (!contentHits.empty()) {
                    hasProblem = true;
                    abnormalDetail << "内容命中敏感词: " << join(contentHits, ", ") << "\n";
                }
                if (!tagsHits.empty()) {
                    hasProblem = true;
                    abnormalDetail << "标签命中敏感词: " << join(tagsHits, ", ") << "\n";
                }

                // 2. 图片识别
                auto imageUrls = extractImageUrls(content);
                for (const auto &url : imageUrls) {
                    std::string res = runPythonAnimalDetection(url);
                    if (res.find("猫") != std::string::npos || res.find("狗") != std::string::npos) {
                        hasProblem = true;
                        abnormalDetail << "图片检测命中动物：";
                        if (res.find("猫") != std::string::npos) abnormalDetail << "猫 ";
                        if (res.find("狗") != std::string::npos) abnormalDetail << "狗 ";
                        abnormalDetail << "\n";
                    }
                }

                // 3. 情绪分析
                std::string emotion = runPythonEmotionDetection(content);
                if (emotion == "消极") {
                    hasProblem = true;
                    abnormalDetail << "情绪分析结果为：消极\n";
                }

                if (hasProblem) {
                    std::string detailStr = abnormalDetail.str();
                    LOG_INFO << "文章存在异常：\n" << detailStr;

                    dbClient->execSqlAsync(
                        "INSERT INTO account_abnormal_record (user_id, account, abnormal_type, abnormal_detail, is_resolved, create_time) "
                        "VALUES (?, ?, ?, ?, ?, NOW())",
                        [](const Result &) {}, [](const DrogonDbException &) {},
                        userId, std::to_string(userId), "SUSPICIOUS_CONTENT-内容异常", detailStr, 0);

                    dbClient->execSqlAsync(
                        "UPDATE articles SET status = 'offline' WHERE id = ?",
                        [](const Result &) {}, [](const DrogonDbException &) {}, articleId);
                    return;
                }

                // 审核通过
                dbClient->execSqlAsync(
                    "UPDATE articles SET status = 'published', published_at = NOW() WHERE id = ?",
                    [](const Result &) {}, [](const DrogonDbException &) {}, articleId);
            }).detach();
        },
        [=](const DrogonDbException &e) {
            Json::Value res;
            res["code"] = 0;
            res["error"] = std::string("Database error: ") + e.base().what();
            auto resp = HttpResponse::newHttpJsonResponse(res);
            resp->addHeader("Access-Control-Allow-Origin", "*");
            resp->setStatusCode(k500InternalServerError);
            callback(resp);
        },
        articleId);
}



void Content::deleteArticle(const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback,
    int articleId)
{
    if (!checkTokenValid(req, callback)) {
        LOG_WARN << "token无效";
        Json::Value result;
        result["code"]=0;
        result["error"] = "token timed out";
        auto resp = HttpResponse::newHttpJsonResponse(result);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
        return;  // Token 无效，已返回响应
    }
    std::string token = req->getHeader("Authorization");
    if (token.empty()) {
        Json::Value result;
        result["code"]=0;
        result["error"] = "No token provided";
        auto resp = HttpResponse::newHttpJsonResponse(result);
        resp->setStatusCode(k401Unauthorized);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        callback(resp);
        return;
    }

    int userId = -1;
    try {
        auto decoded = jwt::decode(token);
        userId = decoded.get_payload_claim("userId").as_number();
    } catch (const std::exception &e) {
        Json::Value result;
        result["code"]=0;
        result["error"] = std::string("Token parse failed: ") + e.what();
        auto resp = HttpResponse::newHttpJsonResponse(result);
        resp->setStatusCode(k401Unauthorized);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        callback(resp);
        return;
    }

    auto dbClient = drogon::app().getDbClient();

    dbClient->execSqlAsync(
    "SELECT author_id FROM articles WHERE id = ?",
    [=](const Result &res) {
        if (res.empty()) {
            Json::Value ret;
            ret["code"]=0;
            ret["error"] = "Article not found";
            auto resp = HttpResponse::newHttpJsonResponse(ret);
            resp->setStatusCode(k404NotFound);
            resp->addHeader("Access-Control-Allow-Origin", "*");
            callback(resp);
            return;
        }

        int authorId = res[0]["author_id"].as<int>();

        // Check if user is admin (role=1)
        dbClient->execSqlAsync(
        "SELECT role FROM users WHERE id = ?",
        [=](const Result &res2) {
            if (res2.empty()) {
                Json::Value ret;
                ret["code"]=0;
                ret["error"] = "User not found";
                auto resp = HttpResponse::newHttpJsonResponse(ret);
                resp->setStatusCode(k401Unauthorized);
                resp->addHeader("Access-Control-Allow-Origin", "*");
                callback(resp);
                return;
            }

            int role = res2[0]["role"].as<int>();
            bool isAdmin = (role == 1);

            if (authorId != userId && !isAdmin) {
                Json::Value ret;
                ret["code"]=0;
                ret["error"] = "No permission to delete this article";
                auto resp = HttpResponse::newHttpJsonResponse(ret);
                resp->setStatusCode(k200OK);
                resp->addHeader("Access-Control-Allow-Origin", "*");
                callback(resp);
                return;
            }

            dbClient->execSqlAsync(
            "DELETE FROM articles WHERE id = ?",
            [callback](const Result &r) {
                Json::Value ret;
                ret["code"]=1;
                ret["message"] = "Deleted successfully";
                auto resp = HttpResponse::newHttpJsonResponse(ret);
                resp->addHeader("Access-Control-Allow-Origin", "*");
                callback(resp);
            },
            [callback](const DrogonDbException &e) {
                Json::Value ret;
                ret["code"]=0;
                ret["error"] = std::string("Delete failed: ") + e.base().what();
                auto resp = HttpResponse::newHttpJsonResponse(ret);
                resp->setStatusCode(k500InternalServerError);
                resp->addHeader("Access-Control-Allow-Origin", "*");
                callback(resp);
            },
            articleId);
        },
        [callback](const DrogonDbException &e) {
            Json::Value ret;
            ret["code"]=0;
            ret["error"] = std::string("Query user role failed: ") + e.base().what();
            auto resp = HttpResponse::newHttpJsonResponse(ret);
            resp->setStatusCode(k500InternalServerError);
            resp->addHeader("Access-Control-Allow-Origin", "*");
            callback(resp);
        },
        userId);
    },
    [callback](const DrogonDbException &e) {
        Json::Value ret;
        ret["code"]=0;
        ret["error"] = std::string("Query article failed: ") + e.base().what();
        auto resp = HttpResponse::newHttpJsonResponse(ret);
        resp->setStatusCode(k500InternalServerError);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        callback(resp);
    },
    articleId);
}

void Content::revokeArticle(const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback,
    int articleId)

{
    if (!checkTokenValid(req, callback)) {
        LOG_WARN << "token无效";
        Json::Value result;
        result["code"]=0;
        result["error"] = "token timed out";
        auto resp = HttpResponse::newHttpJsonResponse(result);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
        return;  // Token 无效，已返回响应
    }
    std::string token = req->getHeader("Authorization");
    if (token.empty()) {
        Json::Value ret;
        ret["code"]=0;
        ret["error"] = "No token provided";
        auto resp = HttpResponse::newHttpJsonResponse(ret);
        resp->setStatusCode(k401Unauthorized);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        return callback(resp);
    }

    int userId = -1;
    try {
        auto decoded = jwt::decode(token);
        userId = decoded.get_payload_claim("userId").as_number();
    } catch (...) {
        Json::Value ret;
        ret["code"]=0;
        ret["error"] = "Invalid token";
        
        auto resp = HttpResponse::newHttpJsonResponse(ret);
        resp->setStatusCode(k401Unauthorized);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        return callback(resp);
    }

    auto dbClient = app().getDbClient();

    dbClient->execSqlAsync(
        "SELECT role FROM users WHERE id = ?",
        [=](const drogon::orm::Result &r) {
            if (r.empty() || r[0]["role"].as<int>() != 1) {
                Json::Value ret;
                ret["code"]=0;
                ret["error"] = "No permission";
                auto resp = HttpResponse::newHttpJsonResponse(ret);
                resp->setStatusCode(k200OK);
                resp->addHeader("Access-Control-Allow-Origin", "*");
                return callback(resp);
            }

            dbClient->execSqlAsync(
            "UPDATE articles SET status = 'offline' WHERE id = ?",
            [=](const drogon::orm::Result &) {
                Json::Value ret;
                ret["code"]=1;
                ret["message"] = "Article revoked";
                auto resp = HttpResponse::newHttpJsonResponse(ret);
                resp->addHeader("Access-Control-Allow-Origin", "*");
                return callback(resp);
            },
            [=](const DrogonDbException &e) {
                Json::Value ret;
                ret["code"]=0;
                ret["error"] = std::string("Database error: ") + e.base().what();
                auto resp = HttpResponse::newHttpJsonResponse(ret);
                resp->addHeader("Access-Control-Allow-Origin", "*");
                resp->setStatusCode(k500InternalServerError);
                return callback(resp);
            },
            articleId);
        },
        [=](const DrogonDbException &e) {
            Json::Value ret;
            ret["code"]=0;
            ret["error"] = std::string("Database error: ") + e.base().what();
            auto resp = HttpResponse::newHttpJsonResponse(ret);
            resp->setStatusCode(k500InternalServerError);
            resp->addHeader("Access-Control-Allow-Origin", "*");
            return callback(resp);
        },
        userId);
}

void Content::listBlogs(const HttpRequestPtr &req,
                        std::function<void(const HttpResponsePtr &)> &&callback,
                        const std::string &account1,
                        const std::string &account2,
                        int role,
                        int page,
                        int perPage)
{
    if (!checkTokenValid(req, callback)) {
        LOG_WARN << "token无效";
        Json::Value result;
        result["code"]=0;
        result["error"] = "token timed out";
        auto resp = HttpResponse::newHttpJsonResponse(result);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
        return;  // Token 无效，已返回响应
    }
    std::string token = req->getHeader("Authorization");
    int userId = -1;
    try {
        auto decoded = jwt::decode(token);
        userId = decoded.get_payload_claim("userId").as_number();
    } catch (...) {
        Json::Value ret;
        ret["code"]=0;
        ret["error"] = "Invalid token";
        
        auto resp = HttpResponse::newHttpJsonResponse(ret);
        resp->setStatusCode(k401Unauthorized);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        return callback(resp);
    }
    LOG_INFO << "[listBlogs] account1: " << account1;
    LOG_INFO << "[listBlogs] account2: " << account2;
    LOG_INFO << "[listBlogs] account2(解码): " << drogon::utils::urlDecode(account2);
    LOG_INFO << "[listBlogs] role: " << role;
    LOG_INFO << "[listBlogs] page: " << page << ", perPage: " << perPage;

    int offset = (page - 1) * perPage;
    auto blogMapper = app().getDbClient();

    auto result = std::make_shared<Json::Value>();
    auto results = std::make_shared<Json::Value>(Json::arrayValue);

    std::atomic<bool> called{false};
    auto safeCallback = [callback, &called](const HttpResponsePtr &resp) {
        if (!called.exchange(true)) {
            callback(resp);
        }
    };

    auto sendError = [safeCallback](const std::string &msg) {
        Json::Value response;
        response["total_count"] = 0;
        response["page"] = 0;
        response["per_page"] = 0;
        response["message"] = msg;
        response["data"]["results"] = Json::arrayValue;

        auto resp = HttpResponse::newHttpJsonResponse(response);
        resp->setStatusCode(k500InternalServerError);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        safeCallback(resp);
    };

    try {
        if (role == 1) {
            // 超管
            if (!account2.empty() && account2 != "null") {
                                std::string searchPattern = "%" + account2 + "%";
                LOG_DEBUG << "Executing SQL: SELECT * FROM articles WHERE account LIKE '" 
           << searchPattern << "'ORDER BY created_at DESC LIMIT " << perPage << " OFFSET " << offset;

                blogMapper->execSqlAsync(
                    "SELECT * FROM articles WHERE account LIKE ? ORDER BY created_at DESC LIMIT ? OFFSET ?",
                    [=](const Result &r) {
                        for (const auto &row : r) {
                            Json::Value blog;
                            blog["id"] = std::to_string(row["id"].as<int>());
                            blog["title"] = row["title"].as<std::string>();
                            blog["content"] = row["content"].as<std::string>();
                            blog["account"] = row["account"].as<std::string>();
                            blog["status"] = (row["status"].as<std::string>() == "published") ? 1 : 0;
                            results->append(blog);
                        }

                        blogMapper->execSqlAsync(
                            "SELECT COUNT(*) AS count FROM articles WHERE account LIKE ?",
                            [=](const Result &countRes) {
                                int totalCount = countRes[0]["count"].as<int>();
                                (*result)["total_count"] = totalCount;
                                (*result)["page"] = page;
                                (*result)["per_page"] = perPage;
                                (*result)["message"] = "success";
                                (*result)["data"]["results"] = *results;

                                auto resp = HttpResponse::newHttpJsonResponse(*result);
                                resp->addHeader("Access-Control-Allow-Origin", "*");
                                safeCallback(resp);
                            },
                            [=](const DrogonDbException &e) {
                                sendError("Count query error: " + std::string(e.base().what()));
                            },
                            searchPattern);
                    },
                    [=](const DrogonDbException &e) {
                        sendError("Query error: " + std::string(e.base().what()));
                    },
                    searchPattern, perPage, offset);
            } else {
                // 查询所有
                blogMapper->execSqlAsync(
                    "SELECT * FROM articles ORDER BY created_at DESC LIMIT ? OFFSET ?",
                    [=](const Result &r) {
                        for (const auto &row : r) {
                            Json::Value blog;
                            blog["id"] = std::to_string(row["id"].as<int>());
                            blog["title"] = row["title"].as<std::string>();
                            blog["content"] = row["content"].as<std::string>();
                            blog["account"] = row["account"].as<std::string>();
                            blog["status"] = (row["status"].as<std::string>() == "published") ? 1 : 0;
                            results->append(blog);
                        }

                        blogMapper->execSqlAsync(
                            "SELECT COUNT(*) AS count FROM articles",
                            [=](const Result &countRes) {
                                int totalCount = countRes[0]["count"].as<int>();
                                (*result)["total_count"] = totalCount;
                                (*result)["page"] = page;
                                (*result)["per_page"] = perPage;
                                (*result)["message"] = "success";
                                (*result)["data"]["results"] = *results;

                                auto resp = HttpResponse::newHttpJsonResponse(*result);
                                resp->addHeader("Access-Control-Allow-Origin", "*");
                                safeCallback(resp);
                            },
                            [=](const DrogonDbException &e) {
                                sendError("Count query error: " + std::string(e.base().what()));
                            });
                    },
                    [=](const DrogonDbException &e) {
                        sendError("Query error: " + std::string(e.base().what()));
                    },
                    perPage, offset);
            }
        } else {
            // 普通用户
            blogMapper->execSqlAsync(
                "SELECT * FROM articles WHERE account = ? ORDER BY created_at DESC LIMIT ? OFFSET ?",
                [=](const Result &r) {
                    for (const auto &row : r) {
                        Json::Value blog;
                        blog["id"] = std::to_string(row["id"].as<int>());
                        blog["title"] = row["title"].as<std::string>();
                        blog["content"] = row["content"].as<std::string>();
                        blog["account"] = row["account"].as<std::string>();
                        blog["status"] = (row["status"].as<std::string>() == "published") ? 1 : 0;
                        results->append(blog);
                    }

                    blogMapper->execSqlAsync(
                        "SELECT COUNT(*) AS count FROM articles WHERE account = ?",
                        [=](const Result &countRes) {
                            int totalCount = countRes[0]["count"].as<int>();
                            (*result)["total_count"] = totalCount;
                            (*result)["page"] = page;
                            (*result)["per_page"] = perPage;
                            (*result)["message"] = "success";
                            (*result)["data"]["results"] = *results;

                            auto resp = HttpResponse::newHttpJsonResponse(*result);
                            resp->addHeader("Access-Control-Allow-Origin", "*");
                            safeCallback(resp);
                        },
                        [=](const DrogonDbException &e) {
                            sendError("Count query error: " + std::string(e.base().what()));
                        },
                        account1);
                },
                [=](const DrogonDbException &e) {
                    sendError("Query error: " + std::string(e.base().what()));
                },
                account1, perPage, offset);
        }
    } catch (const std::exception &e) {
        sendError("Internal server error: " + std::string(e.what()));
    }
}

void Content::editArticle(const HttpRequestPtr &req,
                         std::function<void(const HttpResponsePtr &)> &&callback,
                         int articleId)
{
    if (!checkTokenValid(req, callback)) {
        LOG_WARN << "token无效";
        Json::Value result;
        result["code"]=0;
        result["error"] = "token timed out";
        auto resp = HttpResponse::newHttpJsonResponse(result);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
        return;  // Token 无效，已返回响应
    }
    // 1. 处理Token
    std::string token = req->getHeader("Authorization");
    if (token.empty()) {
        Json::Value result;
        result["code"]=0;
        result["error"] = "缺少Token";
        auto resp = HttpResponse::newHttpJsonResponse(result);
        resp->setStatusCode(k401Unauthorized);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        callback(resp);
        return;
    }
    
    // 移除"Bearer "前缀（如果有）
    const std::string bearerPrefix = "Bearer ";
    if (token.find(bearerPrefix) == 0) {
        token = token.substr(bearerPrefix.length());
    }

    // 2. 解析Token获取用户ID
    int userId = -1;
    try {
        auto decoded = jwt::decode(token);
        // 处理用户ID可能是字符串或数字的情况
        auto claim = decoded.get_payload_claim("userId");
        if (claim.get_type() == jwt::json::type::string) {
            userId = std::stoi(claim.as_string());
        } else {
            userId = claim.as_number();
        }
    } catch (const std::exception &e) {
        Json::Value result;
        result["code"]=0;
        result["error"] = std::string("Token解析失败: ") + e.what();
        auto resp = HttpResponse::newHttpJsonResponse(result);
        resp->setStatusCode(k401Unauthorized);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        callback(resp);
        return;
    }

    // 3. 解析JSON数据
    auto json = req->getJsonObject();
    if(!json) {
        LOG_ERROR << "editArticle: JSON解析失败";
        Json::Value result;
        result["code"]=0;
        result["error"] = "JSON格式无效";
        auto resp = HttpResponse::newHttpJsonResponse(result);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        resp->setStatusCode(k400BadRequest);  // 从401改为400
        callback(resp);
        return;
    }

    std::string title = (*json)["title"].asString();
    std::string content = (*json)["content"].asString();
    std::string tagsStr = (*json)["tags"].asString();
    
    LOG_INFO << "editArticle: 收到标题: " << title;
    LOG_INFO << "editArticle: 内容长度: " << content.length();
    LOG_INFO << "editArticle: 收到标签: " << tagsStr;




    // 4. 验证必填字段
    if (title.empty() || content.empty()) {
        Json::Value result;
        result["code"]=0;
        result["error"] = "标题和内容不能为空";
        auto resp = HttpResponse::newHttpJsonResponse(result);
        resp->setStatusCode(k400BadRequest);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        callback(resp);
        return;
    }

    auto dbClient = drogon::app().getDbClient();

    // 5. 先检查文章是否存在且属于该用户
    dbClient->execSqlAsync(
        "SELECT id FROM articles WHERE id = ? AND author_id = ?",
        [=](const Result &result) {
            if (result.empty()) {
                Json::Value ret;
                ret["code"]=0;
                ret["message"] = "文章不存在或没有权限";
                auto resp = HttpResponse::newHttpJsonResponse(ret);
                resp->setStatusCode(k200OK);
                resp->addHeader("Access-Control-Allow-Origin", "*");
                callback(resp);
                return;
            }

            // 6. 更新文章
            dbClient->execSqlAsync(
                "UPDATE articles SET title = ?, content = ?, tags = ?, updated_at = NOW() WHERE id = ?",
                [=](const Result &r2) {
                    // 检查是否实际更新了数据
                    if (r2.affectedRows() == 0) {
                        Json::Value ret;
                        ret["code"]=0;
                        ret["message"] = "文章更新失败-未更改任何数据";
                        auto resp = HttpResponse::newHttpJsonResponse(ret);
                        resp->setStatusCode(k500InternalServerError);
                        resp->addHeader("Access-Control-Allow-Origin", "*");
                        callback(resp);
                        return;
                    }
                    
                    Json::Value ret;
                    ret["code"]=1;
                    ret["message"] = "文章更新成功";
                    ret["data"]["id"] = articleId;
                    auto resp = HttpResponse::newHttpJsonResponse(ret);
                    resp->addHeader("Access-Control-Allow-Origin", "*");
                    callback(resp);
                },
                [=](const DrogonDbException &e) {
                    Json::Value ret;
                    ret["code"]=0;
                    ret["message"] = std::string("数据库更新错误: ") + e.base().what();
                    auto resp = HttpResponse::newHttpJsonResponse(ret);
                    resp->setStatusCode(k500InternalServerError);
                    resp->addHeader("Access-Control-Allow-Origin", "*");
                    callback(resp);
                },
                title, content, tagsStr, articleId);
        },
        [=](const DrogonDbException &e) {
            Json::Value ret;
            ret["code"]=0;
            ret["error"] = std::string("数据库验证错误: ") + e.base().what();
            auto resp = HttpResponse::newHttpJsonResponse(ret);
            resp->addHeader("Access-Control-Allow-Origin", "*");
            resp->setStatusCode(k500InternalServerError);
            callback(resp);
        },
        articleId, userId);
}


void Abnormal::addSensitiveWords(const HttpRequestPtr &req,
                                 std::function<void(const HttpResponsePtr &)> &&callback) {
    if (!checkTokenValid(req, callback)) {
        LOG_WARN << "token无效";
        Json::Value result;
        result["code"]=0;
        result["error"] = "token timed out";
        auto resp = HttpResponse::newHttpJsonResponse(result);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
        return;  // Token 无效，已返回响应
    }
    std::string token = req->getHeader("Authorization");
    if (token.empty()) {
        Json::Value result;
        result["code"]=0;
        result["error"] = "Missing token";
        auto resp = HttpResponse::newHttpJsonResponse(result);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        resp->setStatusCode(k401Unauthorized);
        return callback(resp);
    }

    Json::Value json = *(req->getJsonObject());
    if (!json.isMember("words") || !json["words"].isArray()) {
        Json::Value result;
        result["code"]=0;
        result["error"] = "Invalid format: 'words' array required";
        auto resp = HttpResponse::newHttpJsonResponse(result);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        resp->setStatusCode(k400BadRequest);
        return callback(resp);
    }

    std::vector<std::string> words;
    for (const auto &w : json["words"]) {
        if (w.isString()) words.push_back(w.asString());
    }

    if (words.empty()) {
        Json::Value result;
        result["code"]=0;
        result["error"] = "No valid words provided";
        auto resp = HttpResponse::newHttpJsonResponse(result);
        resp->setStatusCode(k400BadRequest);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        return callback(resp);
    }

    auto dbClient = app().getDbClient();

    for (const auto &word : words) {
        dbClient->execSqlAsync(
             "INSERT IGNORE INTO sensitive_words (word) VALUES (?)",
            [=](const Result &r) {
                LOG_DEBUG << "Inserted sensitive word: " << word;
            },
            [=](const DrogonDbException &e) {
                LOG_ERROR << "Failed to insert word '" << word << "': " << e.base().what();
            },
            word
        );
    }

    LOG_INFO << "Admin added " << words.size() << " sensitive words";

    Json::Value result;
    result["code"]=1;
    result["message"] = "Words submitted for insertion";
    result["count"] = static_cast<int>(words.size());
    auto resp = HttpResponse::newHttpJsonResponse(result);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    callback(resp);
}



void Abnormal::listSensitiveWords(const HttpRequestPtr &req,
                                  std::function<void(const HttpResponsePtr &)> &&callback,
                                  int page,
                                  int perPage)
{
    if (!checkTokenValid(req, callback)) {
        LOG_WARN << "token无效";
        Json::Value result;
        result["code"]=0;
        result["error"] = "token timed out";
        auto resp = HttpResponse::newHttpJsonResponse(result);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
        return;  // Token 无效，已返回响应
    }
        std::string token = req->getHeader("Authorization");
    if (token.empty()) {
        Json::Value result;
        result["code"]=0;
        result["error"] = "Missing token";
        auto resp = HttpResponse::newHttpJsonResponse(result);
        resp->setStatusCode(k401Unauthorized);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        callback(resp);
        return;
    }

    auto dbClient = app().getDbClient();
    if (page < 1) page = 1;
    if (perPage < 1) perPage = 10;
    int offset = (page - 1) * perPage;

    std::string wordEncoded = req->getParameter("words");  // 注意是 words
    std::string word;
    if (!wordEncoded.empty()) {
        // 解码两次
        word = drogon::utils::urlDecode(drogon::utils::urlDecode(wordEncoded));
    }

    LOG_DEBUG << "listSensitiveWords called with decoded word=" << word;


    if (!word.empty()) {
        std::string likeWord = "%" + word + "%";
        dbClient->execSqlAsync(
            "SELECT id, word FROM sensitive_words WHERE word LIKE ? ORDER BY id DESC LIMIT ? OFFSET ?",
            [=](const Result &r) {
                Json::Value data(Json::arrayValue);
                for (auto row : r) {
                    Json::Value item;
                    item["id"] = row["id"].as<std::string>();
                    item["word"] = row["word"].as<std::string>();
                    data.append(item);
                }

                dbClient->execSqlAsync(
                    "SELECT COUNT(*) FROM sensitive_words WHERE word LIKE ?",
                    [=](const Result &countResult) {
                        int total = countResult[0][0].as<int>();
                        Json::Value respJson;
                        respJson["total_count"] = total;
                        respJson["page"] = page;
                        respJson["per_page"] = perPage;
                        respJson["message"] = "ok";
                        respJson["data"]["results"] = data;
                        auto resp = HttpResponse::newHttpJsonResponse(respJson);
                        resp->addHeader("Access-Control-Allow-Origin", "*");
                        callback(resp);
                    },
                    [=](const DrogonDbException &e) {
                        Json::Value err;
                        err["message"] = std::string("DB error: ") + e.base().what();
                        auto resp = HttpResponse::newHttpJsonResponse(err);
                        resp->setStatusCode(k500InternalServerError);
                        resp->addHeader("Access-Control-Allow-Origin", "*");
                        callback(resp);
                    },
                    likeWord
                );
            },
            [=](const DrogonDbException &e) {
                Json::Value err;
                err["code"]=0;
                err["message"] = std::string("DB error: ") + e.base().what();
                auto resp = HttpResponse::newHttpJsonResponse(err);
                resp->setStatusCode(k500InternalServerError);
                resp->addHeader("Access-Control-Allow-Origin", "*");
                callback(resp);
            },
            likeWord, perPage, offset
        );
    } else {
        // 不带 word 查询
        dbClient->execSqlAsync(
            "SELECT id, word FROM sensitive_words ORDER BY id DESC LIMIT ? OFFSET ?",
            [=](const Result &r) {
                Json::Value data(Json::arrayValue);
                for (auto row : r) {
                    Json::Value item;
                    item["id"] = row["id"].as<std::string>();
                    item["word"] = row["word"].as<std::string>();
                    data.append(item);
                }

                dbClient->execSqlAsync(
                    "SELECT COUNT(*) FROM sensitive_words",
                    [=](const Result &countResult) {
                        int total = countResult[0][0].as<int>();
                        Json::Value respJson;
                        respJson["total_count"] = total;
                        respJson["page"] = page;
                        respJson["per_page"] = perPage;
                        respJson["message"] = "ok";
                        respJson["data"]["results"] = data;
                        auto resp = HttpResponse::newHttpJsonResponse(respJson);
                        resp->addHeader("Access-Control-Allow-Origin", "*");
                        callback(resp);
                    },
                    [=](const DrogonDbException &e) {
                        Json::Value err;
                        err["code"]=0;
                        err["error"] = std::string("DB error: ") + e.base().what();
                        auto resp = HttpResponse::newHttpJsonResponse(err);
                        resp->setStatusCode(k500InternalServerError);
                        resp->addHeader("Access-Control-Allow-Origin", "*");
                        callback(resp);
                    });
            },
            [=](const DrogonDbException &e) {
                Json::Value err;
                err["code"]=0;
                err["error"] = std::string("DB error: ") + e.base().what();
                auto resp = HttpResponse::newHttpJsonResponse(err);
                resp->setStatusCode(k500InternalServerError);
                resp->addHeader("Access-Control-Allow-Origin", "*");
                callback(resp);
            },
            perPage, offset);
    }
}


void Abnormal::deleteSensitiveWord(const HttpRequestPtr &req,
                                   std::function<void(const HttpResponsePtr &)> &&callback,
                                   int id) {
    if (!checkTokenValid(req, callback)) {
        LOG_WARN << "token无效";
        Json::Value result;
        result["code"]=0;
        result["error"] = "token timed out";
        auto resp = HttpResponse::newHttpJsonResponse(result);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
        return;  // Token 无效，已返回响应
    }
    std::string token = req->getHeader("Authorization");
    if (token.empty()) {
        Json::Value result;
        result["code"]=0;
        result["error"] = "Missing token";
        auto resp = HttpResponse::newHttpJsonResponse(result);
        resp->setStatusCode(k401Unauthorized);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        return callback(resp);
    }

    if (id <= 0) {
        Json::Value result;
        result["code"]=0;
        result["error"] = "Invalid ID";
        auto resp = HttpResponse::newHttpJsonResponse(result);
        resp->setStatusCode(k400BadRequest);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        return callback(resp);
    }

    auto dbClient = app().getDbClient();

    dbClient->execSqlAsync(
        "DELETE FROM sensitive_words WHERE id = ?",
        [=](const Result &r) {
            Json::Value result;
            if (r.affectedRows() > 0) {
                result["code"]=1;
                result["message"] = "Deleted successfully";
                result["id"] = id;
            } else {
                result["code"]=0;
                result["error"] = "No word found with given ID";
                result["id"] = id;
            }
            auto resp = HttpResponse::newHttpJsonResponse(result);
            resp->addHeader("Access-Control-Allow-Origin", "*");
            callback(resp);
        },
        [=](const DrogonDbException &e) {
            Json::Value err;
            err["code"]=0;
            err["error"] = std::string("Database error: ") + e.base().what();
            auto resp = HttpResponse::newHttpJsonResponse(err);
            resp->setStatusCode(k500InternalServerError);
            resp->addHeader("Access-Control-Allow-Origin", "*");
            callback(resp);
        },
        id
    );
}

void Content::getBlogDetail(const HttpRequestPtr &req,
                            std::function<void(const HttpResponsePtr &)> &&callback,
                            int target) {
    if (!checkTokenValid(req, callback)) {
        LOG_WARN << "token无效";
        Json::Value result;
        result["code"]=0;
        result["error"] = "token timed out";
        auto resp = HttpResponse::newHttpJsonResponse(result);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
        return;  // Token 无效，已返回响应
    }
    std::string token = req->getHeader("Authorization");
    if (token.empty()) {
        Json::Value result;
        result["code"]=0;
        result["error"] = "Missing token";
        auto resp = HttpResponse::newHttpJsonResponse(result);
        resp->setStatusCode(k401Unauthorized);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        return callback(resp);
    }

    if (target <= 0) {
        Json::Value result;
        result["code"]=0;
        result["error"] = "Invalid blog ID";
        auto resp = HttpResponse::newHttpJsonResponse(result);
        resp->setStatusCode(k400BadRequest);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        return callback(resp);
    }

    auto dbClient = app().getDbClient();

    dbClient->execSqlAsync(
        "SELECT id, title, content FROM articles WHERE id = ?",
        [=](const Result &r) {
            if (r.empty()) {
                Json::Value result;
                result["code"]=0;
                result["error"] = "Blog not found";
                auto resp = HttpResponse::newHttpJsonResponse(result);
                resp->setStatusCode(k404NotFound);
                resp->addHeader("Access-Control-Allow-Origin", "*");
                return callback(resp);
            }

            const auto &row = r[0];
            Json::Value data;
            data["id"] = row["id"].as<std::string>();
            data["title"] = row["title"].as<std::string>();
            data["content"] = row["content"].as<std::string>();

            Json::Value result;
            result["data"] = data;
            result["code"]=1;
            result["message"] = "ok";
            auto resp = HttpResponse::newHttpJsonResponse(result);
            resp->addHeader("Access-Control-Allow-Origin", "*");
            callback(resp);
        },
        [=](const DrogonDbException &e) {
            Json::Value err;
            err["code"]=0;
            err["error"] = std::string("Database error: ") + e.base().what();
            auto resp = HttpResponse::newHttpJsonResponse(err);
            resp->setStatusCode(k500InternalServerError);
            resp->addHeader("Access-Control-Allow-Origin", "*");
            callback(resp);
        },
        target
    );
}

