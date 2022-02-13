// thrift文件命名一般都是以.thrift作为后缀：XXX.thrift，可以在该文件的开头为该文件加上命名空间限制，格式为：namespace 语言名称 名称
namespace cpp match_service 

// struct，自定义结构体类型，在IDL中可以自己定义结构体，对应C中的struct，c++中的struct和class，java中的class，注意，在struct定义结构体时需要对每个结构体成员用序号标识：“序号: ”
// i32，32位整形类型，对应C/C++/java中的int类型
// string，字符串类型，注意是全部小写形式
struct User { 
    1: i32 id, 
    2: string name, 
    3: i32 score
}

// 文件中对所有接口函数的描述都放在service中，service的名字可以自己指定，该名字也将被用作生成的特定语言接口文件的名字。
// 接口函数需要对参数使用序号标号，除最后一个接口函数外，要以,结束对函数的描述
// 可以看出，这两个函数包含两个参数，一个是User结构体，其中包含用户的id，name和score，另一个参数是额外信息，便于后续的调试和升级，后续有额外的参数就不用重新写接口，直接传入info中即可
// add_user：user: 添加的用户信息，info: 附加信息，在匹配池中添加一个名用户
// remove_user：user: 删除的用户信息，info: 附加信息，从匹配池中删除一名用户
service Match {
    i32 add_user(1: User user, 2: string info),

    i32 remove_user(1: User user, 2: string info),
}
