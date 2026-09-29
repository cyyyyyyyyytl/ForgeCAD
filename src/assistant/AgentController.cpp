#include "assistant/AgentController.h" // AgentController 类及其两个成员组件的声明。
#include "application/ModelDocument.h" // 删除前读取包含所有下游特征的预览名单。

#include <QJsonDocument>   // 在 JSON 对象和网络传输用字节串之间转换。
#include <QJsonParseError> // 保存 JSON 解析失败的具体状态。
#include <QMessageBox>    // AI 请求删除时，等待用户在本机确认目标特征。
#include <QStringList>    // 把级联删除的特征 ID 排版为逐行清单。

namespace forge::assistant {

// ============================================================
// 构造：装配模型客户端、工具注册表和系统消息
// ------------------------------------------------------------
// document 由 MainWindow 拥有，Controller 只把它交给 ToolRegistry 引用。
// ============================================================
AgentController::AgentController(application::ModelDocument& document,
    QObject* parent, RequestSender sender)
    : QObject(parent)   // 把 Controller 加入 Qt 父子对象树，交给 parent 管理生命周期。
    , client_()        // 构造只负责网络通信的 DeepSeekClient 值成员。
    , document_(document) // 只读查看删除范围，不通过此引用直接修改文档。
    , tools_(document) // 把当前 CAD 文档以非拥有引用注入工具注册表。
    , requestSender_(std::move(sender))
{
    // 系统消息约束模型角色和安全行为：只有工具成功后才能声称已经修改模型。
    messages_.append(QJsonObject{
        {"role", "system"}, // system 消息优先级最高，用来定义助手身份和边界。
        {"content",
         "你是 ForgeCAD AI 建模助手。尺寸单位为毫米。"
         "只能使用提供的工具查询或修改模型；工具返回 success=true 后才能声称操作成功。"
         "工具 success 只表示数据操作成功；应检查 rebuild_status，failed/blocked 不能声称几何生成成功，empty 应解释为空结果。"
         "支持基础体、草图、拉伸、拉伸切除、旋转、布尔、定位、文件、撤销重做及视图工具。"
         "先查询对象和参数规则，按返回的稳定 ID 操作；旋转实体与旋转相机是不同工具。"
         "设计旋转、定位或连接结构前读取 get_spatial_rules（也随 get_feature_types 返回）；只按明确支持的操作规划，不在执行中反复猜测坐标。绕Y的圆环中心圆平行XZ，绕X平行YZ；环心可沿轴偏移，不一定在世界原点。"
         "Revolve 支持 set_position，XYZ为生成后的世界平移量。通用 create_transform 支持实体和线框绕指定pivot依次世界X/Y/Z旋转并平移，保留源依赖；相机旋转不改变实体。以当前工具能力为准，不沿用旧对话中的过期限制。"
         "精确零件缺少必要尺寸时先询问；用户说你直接来、按建议来、自由设计时，已授权你选择合理尺寸与造型，简短说明默认值后立即建模，不再反复询问。"
         "缺少文件路径可省略 path 让本机选文件。"
         "支持Cone圆锥/圆台、XY/XZ/YZ矩形和圆形草图、带直线/圆弧的自定义轮廓ProfileSketch、3D直线/圆弧路径Path3D、圆角Fillet、倒角Chamfer、扫掠Sweep和放样Loft。旋转可绕世界原点X/Y/Z轴，轴须平行草图平面；竖直杯身可使用XZ自定义闭合轮廓绕Z旋转。圆弧bulge=tan(有向角/4)，不要猜其含义。空心杯可直接旋转闭合壁截面。"
         "弯曲杯耳可扫掠圆截面沿3D圆弧路径；喇叭杯身可用圆台、轮廓旋转或有序截面放样，依造型选择。圆角先选合理半径和规则边，指定边先list_edges查询；ID在上游几何变化后可能失效，须重新查询。几何失败时看错误，调整尺寸或选边，不无意义反复重试。"
         "可先旋转出独立工具实体再用差集切除；不能断言所有弧形结构都必须扫掠。"
         "自由设计时先查询现有文档和工具能力，再在首次修改前用2至4句说明设计方案：风格与主体轮廓、整体尺寸与各部分比例、连接或过渡方式、当前工具造成的限制。"
         "方案说明应与即将执行的工具调用一同返回，随后直接执行；用户已授权时不要仅给计划就结束或再次要求确认。简单明确的参数修改无需重复设计方案。"
         "根据用途选择协调的轮廓和比例，不套用固定的奖杯或其他物品配方；每个组成部分应有结构或造型作用，避免堆叠无关基础体来充当细节。"
         "按实际坐标计算连接和空腔关系，连接件不能侵入应保留的内部空间；不支持的曲线过渡明确说明，不把直杆与球体简单相接描述成已做出流畅过渡。"
         "完成前用 analyze_geometry 查询最终结果或读取本机自动几何检查，对照方案核对实际包围盒尺寸、体积和solid_count；一体模型必须single_solid=true，不能用一个Union ID或ready证明单实体。多个实体时先检查和修正连接，不堆叠无关零件补救原方案。"
         "包围盒重叠不证明真实相交；点或线接触不代表可靠连接，单实体也不证明壁厚/强度合格。方案变更时重新说明实际轮廓和尺寸，不把未完成的原方案说成完成。当前工具不返回图片，不得声称已看过成品或验证审美，不无依据评价精美或漂亮。"
         "操作前查询当前文档，沿用工具返回的ID；复杂任务分阶段执行，成功后勿重复创建。中断后用户要求继续时先查询已有对象和重建状态，再补未完成部分，不擅自清空、删除或重做整个模型。"
         "检查 document_status.failed_features，输入修改成功也可能使下游失败。"
         "cancelled=true 时告知已取消且不得重试；pending_user_input=true 仅表示已启动点击定位，等待用户点击，不能声称位置已修改。"
         "删除操作若被用户取消，不要重试，应告知用户未删除。回答使用简洁中文。"},
    });

    // 网络成功和失败分别进入两个处理槽，保持状态收口在 Controller。
    connect(&client_, &DeepSeekClient::responseReceived,
            this, &AgentController::handleResponse);
    connect(&client_, &DeepSeekClient::requestFailed,
            this, &AgentController::handleError);
}

// 接收 UI 输入并启动新一轮 Agent 循环。
void AgentController::submit(const QString& userMessage)
{
    // trim 后为空的输入没有语义，直接忽略且不污染对话历史。
    const QString text = userMessage.trimmed(); // 生成去除首尾空白后的新字符串。
    if (text.isEmpty()) return;                 // 空字符串不进入历史，也不发网络请求。

    // MVP 同一时间只允许一个请求链，避免两轮 tool_call 历史交叉。
    if (busy_) {
        emit errorMessage("上一条请求仍在处理中"); // 通知聊天窗口显示可理解的原因。
        return;                                  // 保留正在运行的请求链，不启动第二条。
    }

    // 对话历史由客户端保存，DeepSeek 每轮都会收到完整 messages。
    messages_.append(QJsonObject{{"role", "user"}, {"content", text}});
    // 新请求重置预算；模型与对话历史保留，继续任务时可查询已完成对象。
    currentStep_ = 0;   // 新用户消息从 Agent 第 0 步重新计数。
    toolCallCount_=0; modelChangeCount_=0; consecutiveFailures_=0;
    changedFeatureIds_.clear(); summaryOnly_=false; stopReason_.clear();
    latestGeometryAnalysis_={};
    setBusy(true);      // 锁住输入框，并向外发出 busyChanged(true)。
    requestNextTurn();  // 发起第一轮“消息 + 工具说明”请求。
}

// 发送当前完整对话历史和最新工具 Schema；成功/失败通过信号异步回来。
void AgentController::requestNextTurn()
{
    // 状态栏显示当前步骤，用户可以看出是否发生了多轮工具调用。
    emit statusMessage(summaryOnly_ ? QStringLiteral("正在整理本轮执行结果…") : QString("正在请求 DeepSeek（工具轮数 %1/%2）…")
                           .arg(currentStep_ + 1)
                           .arg(MaxAgentSteps));
    auto requestMessages=messages_;
    if (modelChangeCount_>0) {
        // 宿主在每次修改后的请求前检查最终几何，不依赖模型主动记得调用工具。
        // 这是只读反馈，不占模型工具预算，不追加永久历史或额外网络轮次。
        latestGeometryAnalysis_=tools_.execute("analyze_geometry",{});
        requestMessages.append(QJsonObject{{"role","system"},{"content",
            QStringLiteral("本机已用 analyze_geometry 检查当前最终结果，事实如下：\n")+
            QString::fromUtf8(QJsonDocument(latestGeometryAnalysis_).toJson(QJsonDocument::Compact))+
            QStringLiteral("\n按实际尺寸和solid_count核对方案。若目标是一体模型而含多个实体，先修正连接再检查。success=false不能声称几何已验收。没有视觉反馈，不评价审美已合格。")}});
    }
    if (summaryOnly_) requestMessages.append(QJsonObject{{"role","system"},{"content",stopReason_+QStringLiteral("\n")+progressSummary()+
        QStringLiteral("\n本轮仅汇报：不得再调用工具或无依据声称整个任务已完成。根据工具结果说明已完成、失败和未完成部分；如需继续，下一次先查询现有模型，勿重复创建。")}});
    // 收尾指令只发送这一轮，不能永久留在 system 历史里阻止用户下一次继续。
    if (requestSender_) requestSender_(requestMessages,tools_.schemas(),!summaryOnly_);
    else client_.send(requestMessages, tools_.schemas(),!summaryOnly_);
}

// ============================================================
// handleResponse：解释一次模型响应，并决定结束还是继续循环
// ------------------------------------------------------------
// 分支 A：没有 tool_calls -> 展示最终文本、结束 busy 状态。
// 分支 B：存在 tool_calls -> 本地执行、追加 tool 结果、再次请求模型。
// ============================================================
void AgentController::handleResponse(const QJsonObject& response)
{
    if (!busy_) return; // 忽略已结束请求的过期响应。
    // Chat Completions 用 choices 数组承载候选结果；MVP 只使用第一个候选。
    const QJsonArray choices = response.value("choices").toArray();
    if (choices.isEmpty() || !choices.first().isObject()) {
        handleError("DeepSeek 响应缺少 choices"); // 统一显示错误并解除 busy 状态。
        return;                                  // 响应结构不可信，不能继续向下取值。
    }

    // choices[0].message 才包含 assistant 文本或 tool_calls。
    const QJsonObject message = choices.first().toObject().value("message").toObject();
    if (message.isEmpty()) {
        handleError("DeepSeek 响应缺少 message"); // choices 存在但缺少真正的回答对象。
        return;                                  // 立即停止本轮解析。
    }

    // toArray() 在字段不存在时得到空数组，因此同一判断同时覆盖“无字段”和“空字段”。
    const QJsonArray toolCalls = message.value("tool_calls").toArray();
    if (summaryOnly_ && !toolCalls.isEmpty()) {
        // 即使服务忽略 tool_choice=none，也不能在收尾轮继续写入。
        finishLocally();
        return;
    }
    if (toolCalls.isEmpty()) {
        // 没有 tool_calls 表示模型已经给出最终自然语言答案，本轮 Agent 结束。
        QString content = message.value("content").toString().trimmed();
        if (summaryOnly_) {
            const auto progress=stopReason_+QStringLiteral("\n")+progressSummary();
            content=content.isEmpty() ? progress : progress+QStringLiteral("\n")+content;
        } else if (modelChangeCount_>0) {
            content+=(content.isEmpty() ? QString{} : QStringLiteral("\n"))+geometrySummary();
        }
        // 保存最终回答，使下一条用户消息仍拥有连续上下文。
        messages_.append(QJsonObject{{"role", "assistant"}, {"content", content}});
        // 空 content 仍给用户一个可见结束提示，防止界面像“卡住”一样无反馈。
        emit assistantMessage(content.isEmpty() ? "请求已完成。" : content);
        emit statusMessage("就绪"); // 主窗口状态栏恢复为普通待命状态。
        setBusy(false);            // 重新启用输入框和发送按钮。
        return;                    // 最终文本已产生，本次 Agent 循环结束。
    }

    // 必须把包含 tool_calls 的 assistant 消息原样加入历史；下一轮 tool 结果
    // 通过 tool_call_id 与它配对，否则模型 API 无法识别工具调用上下文。
    QJsonObject assistantHistory{
        {"role", "assistant"},
        {"content", message.value("content")},
        {"tool_calls", toolCalls},
    };
    messages_.append(assistantHistory); // 先保存调用请求，后面的 tool 结果才能按 ID 配对。

    // 同一条响应可同时说明设计方案并调用工具；在修改模型前显示说明。
    // 保留 busy 状态继续执行，避免用户只看到最后总结，无法对照方案与成品。
    const auto explanation = message.value("content").toString().trimmed();
    if (!explanation.isEmpty()) emit assistantMessage(explanation);

    // 一次响应可以请求多个工具，例如先创建 Box 再创建 Sphere，逐个执行并回传。
    QString stopAfterBatch;
    for (const QJsonValue& rawCall : toolCalls) {
        // API 外层 call 提供配对 ID，function 内层提供工具名和 JSON 字符串参数。
        const QJsonObject call = rawCall.toObject();
        const QString callId = call.value("id").toString();
        const QJsonObject function = call.value("function").toObject();
        const QString toolName = function.value("name").toString();
        const QByteArray argumentBytes = function.value("arguments").toString().toUtf8();

        // function.arguments 在 API 响应里是“包含 JSON 的字符串”，仍可能格式错误，
        // 所以这里必须再次解析并验证，不能直接信任模型输出。
        QJsonParseError parseError; // fromJson 会把成功或失败状态写入这个输出参数。
        const QJsonDocument argumentDocument = QJsonDocument::fromJson(argumentBytes, &parseError);
        QJsonObject result; // 无论解析成功与否，都构造统一 JSON 结果回传给模型。
        if (!stopAfterBatch.isEmpty()) {
            // 全部 tool_call_id 都要配上结果；剩余调用跳过，不留下悬空协议消息。
            result={{"success",false},{"skipped",true},{"error",stopAfterBatch}};
        } else if (parseError.error != QJsonParseError::NoError || !argumentDocument.isObject()) {
            result = {
                {"success", false},
                {"error", "工具 arguments 不是合法 JSON 对象"},
            };
        } else {
            result = executeTool(toolName, argumentDocument.object());
        }
        if (!result.value("skipped").toBool()) {
            ++toolCallCount_;
            if (result.value("model_changed").toBool()) {
                ++modelChangeCount_;
                const auto id=result.value("feature_id").toString();
                if (!id.isEmpty() && !changedFeatureIds_.contains(id)) changedFeatureIds_.append(id);
            }
            const auto state=result.value("rebuild_status").toString();
            const bool geometryFailed=state=="failed" || state=="blocked" ||
                !result.value("document_status").toObject().value("failed_features").toArray().isEmpty();
            consecutiveFailures_=result.value("success").toBool() && !geometryFailed ? 0 : consecutiveFailures_+1;
            if (result.value("cancelled").toBool()) stopAfterBatch=QStringLiteral("用户取消了操作，本轮后续工具已停止。");
            else if (result.value("pending_user_input").toBool()) stopAfterBatch=QStringLiteral("已启动点击定位，正在等待用户点击；后续工具已停止。");
            else if (consecutiveFailures_>=MaxConsecutiveFailures) stopAfterBatch=QStringLiteral("连续三次工具或几何失败，本轮后续工具已停止，请先检查参数或现有模型。");
            else if (toolCallCount_>=MaxToolCalls) stopAfterBatch=QStringLiteral("达到本轮 96 次工具调用上限，已保留执行结果；未完成部分可在查询模型后继续。");
        }

        // 工具返回值不是给 UI 的最终回答，而是作为 role=tool 再喂给模型，
        // 让模型判断是否继续调用工具，或向用户总结执行结果。
        messages_.append(QJsonObject{
            {"role", "tool"},
            {"tool_call_id", callId},
            {"content", QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact))},
        });
    }

    // 一批 tool_calls 算作一步。达到上限时熔断，不再向服务端继续递归请求。
    ++currentStep_; // 一次响应中的多个并行 tool_calls 合计为一个 Agent 步骤。
    if (!stopAfterBatch.isEmpty()) { finishWithSummary(stopAfterBatch); return; }
    if (currentStep_ >= MaxAgentSteps) {
        finishWithSummary(QStringLiteral("达到本轮 24 轮工具上限，已保留执行结果；未完成部分可在查询模型后继续。"));
        return;
    }
    requestNextTurn(); // 把刚追加的 role=tool 结果发回模型，请它继续判断或总结。
}

QJsonObject AgentController::executeTool(const QString& name, const QJsonObject& arguments)
{
    QJsonObject result;
    try {
        // 校验先于任何确认框，错误请求不会误删或弹无意义提示。
        tools_.validateArguments(name,arguments);
        if (name=="delete_feature") {
            const auto id=arguments.value("feature_id").toString().trimmed();
            const auto target=tools_.execute("get_feature",{{"feature_id",id}});
            if (!target.value("success").toBool()) return target;
            QStringList affected;
            for (const auto& candidate : document_.deletionOrder(id.toStdString()))
                if (candidate!=id.toStdString()) affected.append(QString::fromStdString(candidate));
            auto prompt=QString("确定删除 %1（%2）吗？").arg(id,target.value("type").toString());
            if (!affected.isEmpty()) prompt+=QString("\n\n以下依赖特征也会一起删除：\n%1").arg(affected.join("\n"));
            if (QMessageBox::question(qobject_cast<QWidget*>(parent()),"确认删除",prompt,
                QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)
                return {{"success",false},{"cancelled",true},{"error","用户取消删除"}};
        }
        result=tools_.execute(name,arguments);
    } catch (const std::exception& error) {
        result={{"success",false},{"error",QString::fromUtf8(error.what())}};
    }
    emit statusMessage(QString("工具%1：%2").arg(result.value("success").toBool() ? "成功" : "未完成",name));
    if (result.value("model_changed").toBool()) emit modelChanged(result.value("feature_id").toString());
    return result;
}

// 所有网络/协议错误统一结束 busy 状态并通过两个信号分别更新对话框和状态栏。
void AgentController::handleError(const QString& error)
{
    if (!busy_) return;
    if (summaryOnly_) {
        finishLocally(); // 网络总结失败仍给出本机进度，不丢失中断原因。
        emit errorMessage(QStringLiteral("AI 总结失败：")+error);
        return;
    }
    if (toolCallCount_>0) {
        const auto progress=QStringLiteral("请求中断，已执行的操作保留。\n")+progressSummary();
        messages_.append(QJsonObject{{"role","assistant"},{"content",progress}});
        emit assistantMessage(progress);
    }
    emit errorMessage(error);       // 对话框显示详细错误内容。
    emit statusMessage("请求失败"); // 主窗口状态栏显示简短状态。
    setBusy(false);                 // 无论哪种错误都必须重新允许用户输入。
}

QString AgentController::progressSummary() const
{
    int failures=0;
    for (const auto& [id,result] : document_.rebuildReport()) if (!result.usable()) ++failures;
    auto progress=QStringLiteral("本轮执行 %1 次工具调用，记录 %2 次模型修改；当前文档有 %3 个特征，重建失败或阻塞 %4 个。")
        .arg(toolCallCount_).arg(modelChangeCount_).arg(document_.features().size()).arg(failures);
    if (!changedFeatureIds_.isEmpty()) progress+=QStringLiteral("\n本轮涉及的特征（含可能已删除对象）：")+changedFeatureIds_.join(QStringLiteral("、"));
    if (!latestGeometryAnalysis_.isEmpty()) progress+=QStringLiteral("\n")+geometrySummary();
    return progress;
}

QString AgentController::geometrySummary() const
{
    if (!latestGeometryAnalysis_.value("success").toBool())
        return QStringLiteral("本机几何检查未通过：")+latestGeometryAnalysis_.value("error").toString();
    const auto geometry=latestGeometryAnalysis_.value("geometry").toObject();
    if (geometry.value("empty").toBool()) return QStringLiteral("本机几何检查：最终结果为空，没有实体。");
    const auto size=geometry.value("bounds_mm").toObject().value("size").toObject();
    auto text=QStringLiteral("本机几何检查：最终结果含 %1 个实体；XYZ 尺寸 %2 × %3 × %4 mm。")
        .arg(geometry.value("solid_count").toInt()).arg(size.value("x").toDouble(),0,'g',8)
        .arg(size.value("y").toDouble(),0,'g',8).arg(size.value("z").toDouble(),0,'g',8);
    if (geometry.value("solid_count").toInt()>1) text+=QStringLiteral("尚不能称为单一实体。");
    if (!geometry.value("solid_body").toBool()) text+=QStringLiteral("含非实体几何，整体体积不可用。");
    return text;
}

void AgentController::finishWithSummary(const QString& reason)
{
    summaryOnly_=true; stopReason_=reason;
    requestNextTurn();
}

void AgentController::finishLocally()
{
    const auto progress=stopReason_+QStringLiteral("\n")+progressSummary();
    messages_.append(QJsonObject{{"role","assistant"},{"content",progress}});
    emit assistantMessage(progress);
    emit statusMessage(QStringLiteral("本轮已停止，执行结果已保留"));
    setBusy(false);
}

// 集中维护 busy_，只有状态真实变化才发信号，避免 UI 重复启停输入控件。
void AgentController::setBusy(bool busy)
{
    if (busy_ == busy) return; // 状态没有变化时不重复发信号，减少无意义的 UI 更新。
    busy_ = busy;              // 先更新内部真值，槽函数收到信号时查询结果才一致。
    emit busyChanged(busy_);   // 通知 AssistantDialog 启用或禁用交互控件。
}

} // namespace forge::assistant
