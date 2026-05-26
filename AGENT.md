# M1Switch Agent 规则文件


---

## 规则 1：中文强制规则

**所有推理上下文、思考过程、文档、代码注释、TODO 注释必须使用中文。**

- Agent 在推理时（thinking）、编写文档、添加代码注释时，**必须使用简体中文**。
- 代码本身（变量名、函数名、类名）保持英文不变。
- 对外输出的最终总结可以使用中英文混合，但内部推理必须为中文。
- 代码注释格式示例：

```cpp
// ✅ 正确：中文注释
// 初始化 Metal 设备并创建命令队列
bool InitMetalDevice();

// ❌ 错误：英文注释
// Initialize Metal device and create command queue
bool InitMetalDevice();
```

- TODO 注释格式示例：

```cpp
// ✅ 正确
// TODO: 实现纹理缓存回收策略

// ❌ 错误
// TODO: Implement texture cache eviction strategy
```

---

## 规则 2：README 进度更新规则

**每个任务完成后，必须更新 `README.md` 中的 "Project Status" 部分。**

- 在 `README.md` 的 `## Project Status` 章节中，找到对应的 Phase，将已完成的任务标记为 `[x]`。
- 如果任务属于新的 Phase，添加对应的 Phase 章节和条目。
- 更新完成后，简要确认进度变更内容。

---

## 规则 3：自动 Git 推送规则

**每个任务完成后，必须自动执行 `git add`、`git commit` 和 `git push`。**

- 提交信息（commit message）使用中文，简洁描述本次变更内容。
- 推送前确认当前分支名称（默认 `main`）。
- 推送流程：

```bash
git add -A
git commit -m "<中文提交信息>"
git push origin <当前分支>
```

- 如果推送失败（如网络问题、冲突），向用户报告错误原因。

---

## 规则执行顺序

每个任务完成后，按以下顺序执行：

1. **代码变更**：完成用户的代码需求
2. **README 更新**：根据规则 2 更新进度
3. **Git 提交**：根据规则 3 提交并推送代码
4. **总结反馈**：向用户简要总结完成内容

---

## 示例工作流

```
用户请求 → Agent 用中文推理 → 实现代码变更
  → 更新 README.md 进度
  → git add -A && git commit -m "..." && git push
  → 总结反馈
```
