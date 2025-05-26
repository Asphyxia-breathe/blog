sudo apt install git gcc g++ cmake
sudo apt install libjsoncpp-dev
sudo apt install uuid-dev
sudo apt install zlib1g-dev
sudo apt install openssl libssl-dev
sudo apt install -y libmysqlclient-dev 
sudo apt install mysql-server
sudo apt install curl
apt install libpq-dev
apt-get install zip
apt install libmariadb-dev
 apt install libsqlite3-dev
sudo apt-get install pkg-config
sudo apt install build-essential
sudo apt-get install libyaml-cpp-dev

## 启动并连接mysql
我用的docker遇到了一个问题

```
root@cf7a1e4ea733:/home/drogon/build# service mysql start
 * Starting MySQL database server mysqld                                                                                                        su: warning: cannot change directory to /nonexistent: No such file or directory
```
解决方法

```
service mysql stop
usermod -d /var/lib/mysql/ mysql
service mysql start
```

然后本地使用navicat连接mysql出现了问题# ERROR 1698 (28000): Access denied for user 'root'@'localhost'
解决方法，

```
//666是密码
ALTER USER 'root'@'localhost' IDENTIFIED WITH mysql_native_password BY '666';
FLUSH PRIVILEGES;
```
mysql -u root -p
输入密码进入
CREATE DATABASE IF NOT EXISTS blog_db;

## 安装vcpkg及jsoncpp及jwt-cpp
cd vcpkg
./bootstrap-vcpkg.sh
./vcpkg integrate install
./vcpkg install jwt-cpp
./vcpkg install jsoncpp

## 安装drogon
cd drogon


## 编译项目
cmake ..   -DCMAKE_TOOLCHAIN_FILE=/home/leike/feishu-blog/feishu-project/backend-blog/vcpkg/scripts/buildsystems/vcpkg.cmake   -Djwt-cpp_DIR=/home/leike/feishu-blog/feishu-project/backend-blog/vcpkg/installed/x64-linux/share/jwt-cpp   -DCMAKE_PREFIX_PATH=/home/leike/feishu-blog/feishu-project/backend-blog/vcpkg/installed/x64-linux
