// =============================================================================
// selftest.cpp —— 无头自测（不建窗口），只测纯模型层：
//   解析 / Value 归一 / Limit 解析 / 校验 / 权限 / 回写 / 键顺序与未知字段保真
//   用法：parm_ui_selftest [sample_all_types.json]
// =============================================================================

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "param_model.h"

using namespace parm_ui;

namespace {

int g_pass = 0;
int g_fail = 0;

void check(bool cond, const std::string& what) {
    if (cond) {
        ++g_pass;
        std::printf("  ok   %s\n", what.c_str());
    } else {
        ++g_fail;
        std::printf("  FAIL %s\n", what.c_str());
    }
}

bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

std::size_t pos_of(const std::string& hay, const std::string& needle) {
    return hay.find(needle);
}

Node* find_root(Doc& doc, const std::string& key) {
    for (Node& n : doc.roots)
        if (n.key == key) return &n;
    return nullptr;
}

std::string read_file(const std::string& path, bool& ok) {
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) {
        ok = false;
        return std::string();
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    ok = true;
    return ss.str();
}

// ---------------------------------------------------------------------------
void test_order_and_unknown_fields() {
    std::printf("[1] 键顺序 / 未知字段 / 数字字面量 保真\n");
    const std::string src =
        R"({"B":{"z":1,"a":2},"A":{"Type":"int","Competence":0,"Limit":[0,10],"Value":5,"Comment":"c"},"C":true,"F":{"Type":"float","Competence":0,"Limit":"","Value":1.0e2},"G":{"Type":"float","Competence":0,"Limit":"","Value":0.500}})";

    Doc         doc;
    std::string err;
    check(doc_load(doc, src, err), "加载成功");
    const std::string out = doc_save(doc);

    check(pos_of(out, "\"B\"") < pos_of(out, "\"A\"") && pos_of(out, "\"A\"") < pos_of(out, "\"C\""),
          "顶层键顺序保持 B -> A -> C -> F -> G");
    check(pos_of(out, "\"z\"") < pos_of(out, "\"a\""), "子对象键顺序保持 z -> a");
    check(contains(out, "\"Comment\""), "未知字段 Comment 原样保留");
    check(pos_of(out, "\"Value\"") < pos_of(out, "\"Comment\""), "未知字段位置不变（Value 之后）");
    check(contains(out, "1.0e2"), "浮点字面量 1.0e2 未被重新格式化");
    check(contains(out, "0.500"), "浮点字面量 0.500 未被重新格式化");

    Json rt;
    check(Json::parse(out, rt, err) && json_equal(doc.raw, rt), "序列化结果再解析后结构等价");
}

// ---------------------------------------------------------------------------
void test_robustness() {
    std::printf("[2] 节点级健壮性（单个节点坏了不影响整棵树）\n");
    const std::string src = R"({
      "G1":{"TTT":{"Value":null}},
      "P1":{"Type":"int","Competence":1,"Limit":[0,10],"Value":5},
      "P2":{"Competence":1,"Limit":[0,10],"Value":5},
      "P3":{"Type":"matrix","Competence":1,"Limit":[0,10],"Value":5},
      "P4":{"Type":"int","Competence":1,"Value":5},
      "P5":{"Type":"int","Competence":1,"Limit":[0,10]},
      "P6":{"Type":"bool","Competence":1,"Limit":"","Value":true}
    })";

    Doc         doc;
    std::string err;
    check(doc_load(doc, src, err), "加载成功（没有因为坏节点整体失败）");
    check(doc.count_fatal() == 4, "4 个节点被判为致命并跳过（TTT/P2/P3/P5）");
    check(doc.count_warning() == 1, "1 条提示（P4 缺 Limit，仅告警不跳过）");

    Node* p1 = find_root(doc, "P1");
    check(p1 != nullptr && p1->ok && p1->is_param && p1->param.type == PType::Int,
          "P1 正常解析为 int 参数");

    Node* p6 = find_root(doc, "P6");
    check(p6 != nullptr && p6->ok && p6->param.value.as_string() == "TRUE",
          "P6 的 JSON true 被归一成字符串 \"TRUE\"（按规范 bool 用 TRUE/FALSE）");

    Node* p4 = find_root(doc, "P4");
    check(p4 != nullptr && p4->ok && !p4->param.has_range, "P4 缺 Limit 时不带范围限制");

    const std::string out = doc_save(doc);
    check(contains(out, "\"Value\": 5") && contains(out, "\"Value\": null"),
          "坏节点在保存时原样保留，没有被改动或删除");
}

// ---------------------------------------------------------------------------
void test_normalize() {
    std::printf("[3] Value 归一\n");
    const std::string src = R"({
      "S2I":{"Type":"int","Competence":1,"Limit":[0,100],"Value":"60"},
      "I2F":{"Type":"float","Competence":1,"Limit":"","Value":3},
      "B2S":{"Type":"bool","Competence":1,"Limit":"","Value":"false"},
      "UNK":{"Type":"int","Competence":1,"Limit":[0,10]},
      "ARR":{"Type":"string[]","Competence":1,"Limit":"","Value":["A",1,true]},
      "DUP":{"Type":"int","Competence":1,"Limit":"","Value":1.5}
    })";

    Doc         doc;
    std::string err;
    check(doc_load(doc, src, err), "加载成功");

    Node* s2i = find_root(doc, "S2I");
    check(s2i && s2i->ok && s2i->param.value.kind == Json::Kind::Int && s2i->param.value.i == 60,
          "字符串 \"60\" 归一为整数 60");

    Node* i2f = find_root(doc, "I2F");
    check(i2f && i2f->ok && i2f->param.value.kind == Json::Kind::Double &&
              i2f->param.value.d == 3.0,
          "整数 3 归一为浮点 3.0");

    Node* b2s = find_root(doc, "B2S");
    check(b2s && b2s->ok && b2s->param.value.as_string() == "FALSE",
          "字符串 \"false\" 归一为大写 \"FALSE\"");

    Node* unk = find_root(doc, "UNK");
    check(unk != nullptr && !unk->ok, "缺 Value 的节点标记为解析失败（不渲染）");

    Node* arr = find_root(doc, "ARR");
    check(arr && arr->ok && arr->param.value.is_array() && arr->param.value.arr.size() == 3 &&
              arr->param.value.arr[0].as_string() == "A" &&
              arr->param.value.arr[1].as_string() == "1" &&
              arr->param.value.arr[2].as_string() == "true",
          "string[] 里的数字/布尔元素转成文本");

    Node* dup = find_root(doc, "DUP");
    check(dup != nullptr && !dup->ok, "整数类型遇到 1.5 判为解析失败并给出错误");
}

// ---------------------------------------------------------------------------
void test_parse_errors() {
    std::printf("[4] 整体性错误\n");
    Doc         doc;
    std::string err;
    check(!doc_load(doc, "{ bad json", err) && !err.empty(), "语法错误被报出");
    check(!doc_load(doc, "[1,2,3]", err), "根不是对象被拒绝");
    check(!doc_load(doc, "", err), "空输入被拒绝");
}

// ---------------------------------------------------------------------------
void test_validate_and_permission() {
    std::printf("[5] 校验 + 权限（只读节点不校验、不回写）\n");
    const std::string src = R"({
      "R":{"Type":"int","Competence":1,"Limit":[0,10],"Value":999},
      "E":{"Type":"string_enum","Competence":1,"Limit":["A","B"],"Value":"C"},
      "OK":{"Type":"int","Competence":1,"Limit":[0,10],"Value":5},
      "RO":{"Type":"int","Competence":3,"Limit":[0,10],"Value":999}
    })";

    Doc         doc;
    std::string err;
    check(doc_load(doc, src, err), "加载成功");

    doc.user_competence = 1;  // 只有 RO（需要 3）不可编辑
    std::vector<std::string> errors, warnings;
    doc_validate(doc, errors, warnings);
    check(errors.size() == 2, "越界 + 枚举不在候选 = 2 条错误（只读的 RO 不计）");
    if (errors.size() == 2) {
        std::printf("       -> %s\n       -> %s\n", errors[0].c_str(), errors[1].c_str());
    }

    // 越权节点即使被改了也不能写回
    Node* ro = find_root(doc, "RO");
    if (ro != nullptr) ro->param.value = Json::make_int(7);
    Node* ok_node = find_root(doc, "OK");
    if (ok_node != nullptr) ok_node->param.value = Json::make_int(8);

    const std::string out = doc_save(doc);
    check(contains(out, "\"Value\": 999"), "越权节点保存时保持原值 999");
    check(!contains(out, "\"Value\": 7"), "越权节点被改的值没有被写回");
    check(contains(out, "\"Value\": 8"), "有权节点的新值 8 正常写回");

    doc.user_competence = 3;
    doc_validate(doc, errors, warnings);
    check(errors.size() == 2, "权限提升到 3 后 RO 的越界值也参与校验");
}

// ---------------------------------------------------------------------------
void test_array_and_path_warnings() {
    std::printf("[6] 数组逐元素校验 + 路径存在性只警告\n");
    const std::string src = R"({
      "A":{"Type":"int[]","Competence":1,"Limit":[0,10],"Value":[1,2,3]},
      "P":{"Type":"path","Competence":1,"Limit":"","Value":"./不存在的目录/不存在.hobj"}
    })";

    Doc         doc;
    std::string err;
    check(doc_load(doc, src, err), "加载成功");

    Node* a = find_root(doc, "A");
    check(a != nullptr && a->ok && a->param.value.arr.size() == 3, "int[] 三个元素");
    if (a != nullptr) a->param.value.arr[1] = Json::make_int(999);

    doc.user_competence = 3;
    std::vector<std::string> errors, warnings;
    doc_validate(doc, errors, warnings);
    check(errors.size() == 1 && errors[0].find("第 2 个元素") != std::string::npos,
          "数组元素越界被精确定位到第 2 个元素");
    check(warnings.size() == 1, "不存在的 path 只产生警告，不阻断保存");
}

// ---------------------------------------------------------------------------
void test_sample_file(const std::string& path) {
    // MSVC 的 printf 格式检查器对多字节字符不友好，占位符一律放最前面
    std::printf("[7] sample file round-trip: %s\n", path.c_str());
    bool        ok = false;
    std::string text = read_file(path, ok);
    check(ok, "读到样例文件");
    if (!ok) return;

    Doc         doc;
    std::string err;
    check(doc_load(doc, text, err), "样例加载成功");
    std::printf("       %d 个节点跳过 / %d 条提示\n", doc.count_fatal(), doc.count_warning());
    check(doc.count_fatal() == 4, "样例里故意留了 4 个坏节点（缺 Type / 未知类型 / 缺 Value / TTT 非对象）");

    // 键顺序保真：原树与往返后的树，键路径序列必须完全一致
    Json        original;
    check(Json::parse(text, original, err), "原文件解析成功");
    std::vector<std::string> pa, pb;
    collect_key_paths(original, "", pa);

    const std::string out = doc_save(doc);
    Json              rt;
    check(Json::parse(out, rt, err), "保存结果可再解析");
    collect_key_paths(rt, "", pb);
    check(pa == pb, "全部键路径（含顺序）完全一致");

    // 全部类型都在样例里出现
    int types = 0;
    const char* names[] = {"int", "float", "bool", "string", "string_enum", "int_enum",
                           "float_enum", "int[]", "float[]", "string[]", "directory", "path"};
    for (const char* want : names)
        for (const Node& g : doc.roots)
            for (const Node& c : g.children)
                if (c.is_param && c.param.type_text == want) ++types;
    check(types >= 12, "样例覆盖全部 12 种数据类型");
}

}  // namespace

int main(int argc, char** argv) {
    std::printf("parm_ui 模型层自测\n==================\n");
    test_order_and_unknown_fields();
    test_robustness();
    test_normalize();
    test_parse_errors();
    test_validate_and_permission();
    test_array_and_path_warnings();
    if (argc > 1) test_sample_file(argv[1]);
    std::printf("==================\n通过 %d，失败 %d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
