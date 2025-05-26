#pragma once
#include <drogon/HttpController.h>  // 注意这里是 HttpController！
#include <unordered_map>
#include <string>
#include <vector>
#include <set>
#include<algorithm>
using namespace drogon;

class Content : public HttpController<Content>
{
public:
    Content()
    {
        LOG_DEBUG << "Blog constructor!";
    }


    void asyncHandleHttpRequest(const HttpRequestPtr& req, std::function<void (const HttpResponsePtr &)> &&callback) ;

    

    void createArticle(const HttpRequestPtr &req,
                       std::function<void(const HttpResponsePtr &)> &&callback);

    void publishArticle(const HttpRequestPtr &req,
                        std::function<void(const HttpResponsePtr &)> &&callback,int articleId);
    void deleteArticle(const HttpRequestPtr &req,
        std::function<void(const HttpResponsePtr &)> &&callback,
        int articleId);
    void revokeArticle(const HttpRequestPtr &req,
        std::function<void(const HttpResponsePtr &)> &&callback,
        int articleId);
    void listBlogs(const HttpRequestPtr &req,
        std::function<void(const HttpResponsePtr &)> &&callback,
        const std::string &account1,
        const std::string &account2,
        int role,
        int page,
        int perPage);
    void editArticle(const HttpRequestPtr &req,
                        std::function<void(const HttpResponsePtr &)> &&callback,int articleId);
    void getBlogDetail(const HttpRequestPtr &req,std::function<void(const HttpResponsePtr &)> &&callback,int target) ;
    void exportPdf(const HttpRequestPtr &req,
                            std::function<void(const HttpResponsePtr &)> &&callback,
                            int articleId);
    METHOD_LIST_BEGIN
        METHOD_ADD(Content::asyncHandleHttpRequest,"/test",Get);
        METHOD_ADD(Content::createArticle, "create", {Post,Options});
        METHOD_ADD(Content::publishArticle, "publish/{1}", {Get, Options});
        METHOD_ADD(Content::deleteArticle, "delete/{1}", {Delete,Options});
        METHOD_ADD(Content::revokeArticle, "revoke/{1}", {Get, Options});
        METHOD_ADD(Content::listBlogs,"/list?account1={1}&account2={2}&role={3}&page={4}&per_page={5}",{Get, Options});
        METHOD_ADD(Content::editArticle, "edit/{1}", {Put,Options});
        METHOD_ADD(Content::getBlogDetail, "list/{target}", {Get,Options});
        METHOD_ADD(Content::exportPdf, "exportPdf/{1}", {Get,Options});
        
    METHOD_LIST_END
};

class Abnormal  : public HttpController<Abnormal>{
    public:
    Abnormal()
    {
        LOG_DEBUG << "Abnormal constructor!";
    }
    void addSensitiveWords(const HttpRequestPtr &req,
                                 std::function<void(const HttpResponsePtr &)> &&callback);
    void deleteSensitiveWord(const HttpRequestPtr &req,std::function<void(const HttpResponsePtr &)> &&callback,int id);
    void listSensitiveWords(const HttpRequestPtr &req,std::function<void(const HttpResponsePtr &)> &&callback,int page,int perPage);

    METHOD_LIST_BEGIN
        METHOD_ADD(Abnormal::addSensitiveWords, "sensitive/add", {Post,Options});
        METHOD_ADD(Abnormal::deleteSensitiveWord, "sensitive/delete/{1}", {Delete, Options});
        METHOD_ADD(Abnormal::listSensitiveWords, "sensitive/list?page={1}&perPage={2}", {Get, Options});
    METHOD_LIST_END
};

class SensitiveWordFilter {
public:
    // 从数据库加载敏感词（异步）
    void loadFromDb();
    void loadFromDbSync();    // 同步加载（新增）
    // 检查是否包含敏感词
    std::vector<std::string> detectAllSensitiveWords(const std::string &text) const;
    // 替换敏感词为指定字符
    std::string replaceSensitiveWords(const std::string &text, char replaceChar = '*') const;
    
    // 获取当前加载的敏感词数量
    size_t size() const;

    // 获取单例实例
    static SensitiveWordFilter &instance();

private:
    struct TrieNode {
        std::unordered_map<wchar_t, std::shared_ptr<TrieNode>> wchildren;
        bool isEnd = false;
    };
    std::shared_ptr<TrieNode> root_;
    size_t wordCount_{0};
    mutable std::mutex mutex_;  // 保证线程安全

    SensitiveWordFilter();  // 私有构造函数
    
    void insertWord(const std::string &word);
    bool isSymbol(wchar_t wc) const;
};