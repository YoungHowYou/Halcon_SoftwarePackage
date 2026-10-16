# examples/yolo —— OpenVINO / YOLO26-seg 例程

对应算子：`OpenvinoLoadModel` / `yolo_seg_detect`
（字典一律用本包的 `hdict_*` 句柄，不是 HALCON 原生 `create_dict`）。

## 需要自己放的模型文件

例程里的 `'工程路径'` 指向 **`./best.xml`**（YOLO26-seg，keyence 数据集 3 类）。
OpenVINO IR 需要**同目录同名**的两个文件：

| 文件 | 是否入库 | 说明 |
|---|---|---|
| `best.xml` | 是（626 KB） | 网络结构 + 权重索引；**单独存在跑不了** |
| `best.bin` | **否** | 111 MB 权重，超过 GitHub 单文件 100 MB 硬限制，已在 `.gitignore` 排除 |

⇒ 把 `best.xml` + `best.bin` 一起放进本目录即可；换模型就改例程里的 `.xml` 路径。
（若确实要把权重入库，需要改用 **git-lfs**，届时 `.gitignore` 里那条要换成 `.gitattributes` 的 `filter=lfs`。）

## 例程

| 例程 | 内容 | 说明 |
|---|---|---|
| `1.hdev` | 加载模型 → 逐图 `yolo_seg_detect` → 显示标签图 + 框 Region | **交互式**（`dev_display` / `stop`），把 `list_files` 的路径改成自己的数据集目录 |
| `2.hdev` | 同 1，步长/阈值改过，`stop` 已注释（连续刷图） | 同上 |

## 跑的注意点

* **路径**：例程里是相对路径（`./best.xml`、`./2026_09_04_18_59_26_Gray.png`）。
  HDevelop 里打开本目录的例程直接跑没问题；用命令行 `hrun` 验证时要**改成绝对路径**
  （hrun 的工作目录既不是脚本目录也不是仓库根）。
* `1.hdev` / `2.hdev` 是给真实数据集用的演示，`list_files` 的路径要改成自己的目录。
* 设备：字典的 `'使用设备'` 可取 `'CPU'` / `'GPU'` / `'NPU'`（NPU 首次编译较慢）。
* `'InputImage'` 只接受 **byte**（8 位）灰度或 3 通道图；写别的类型会报错（不静默出错）。
* 同一模型句柄不要跨线程同时用（supply 层已加锁串行化，要并行就每线程各加载一个句柄）。
