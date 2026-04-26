cd %~dp0

cd ..

set inipath=%cd%\my.ini

cd bin

"%cd%\mysqld.exe" -remove mysql33

"%cd%\mysqld.exe" -install mysql33 --defaults-file="%inipath%"

"%cd%\mysqld.exe" --initialize-insecure --console

net start mysql33

sc config mysql33 start=auto

net stop mysql33

net start mysql33

echo 安装完毕

"%cd%\mysqladmin.exe" -u root password 123456 -P3306

echo 修改密码完毕

cd ..

"%cd%\bin\mysql.exe" -u root -p123456 < ".\\initsql\\myInitSql.sql"

echo 数据库初始化完成

echo 完成
