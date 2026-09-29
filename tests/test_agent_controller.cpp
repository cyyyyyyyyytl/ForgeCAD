#include <gtest/gtest.h>
#include "assistant/AgentController.h"
#include "application/ModelDocument.h"
#include "infrastructure/NativeDocumentIO.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <BRepCheck_Analyzer.hxx>
#include <BRepBndLib.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <Bnd_Box.hxx>
#include <TopExp_Explorer.hxx>
#include <gp_Pnt.hxx>

namespace {
void ensureCoreApplication()
{
    static int argc=1;
    static char name[]="agent_loop_test";
    static char* argv[]={name,nullptr};
    static QCoreApplication application(argc,argv);
}
struct Request { QJsonArray messages; bool allowTools; };
struct ScriptedSession {
    forge::application::ModelDocument document;
    std::vector<Request> requests;
    QStringList answers;
    QStringList errors;
    forge::assistant::AgentController agent;
    int nextCall=0;
    ScriptedSession() : agent(document,nullptr,[this](const QJsonArray& messages,const QJsonArray&,bool allowTools) {
        requests.push_back({messages,allowTools});
    }) {
        QObject::connect(&agent,&forge::assistant::AgentController::assistantMessage,[this](const QString& text) { answers.append(text); });
        QObject::connect(&agent,&forge::assistant::AgentController::errorMessage,[this](const QString& text) { errors.append(text); });
    }
    QJsonObject call(const QString& name,const QJsonObject& arguments={}) {
        return {{"id",QString("call_%1").arg(++nextCall)},{"type","function"},
            {"function",QJsonObject{{"name",name},{"arguments",QString::fromUtf8(QJsonDocument(arguments).toJson(QJsonDocument::Compact))}}}};
    }
    bool reply(const QJsonObject& message) {
        const QJsonObject response{{"choices",QJsonArray{QJsonObject{{"message",message}}}}};
        return QMetaObject::invokeMethod(&agent,"handleResponse",Qt::DirectConnection,Q_ARG(QJsonObject,response));
    }
    bool tools(const QJsonArray& calls) { return reply({{"role","assistant"},{"content",QJsonValue::Null},{"tool_calls",calls}}); }
    bool text(const QString& text) { return reply({{"role","assistant"},{"content",text}}); }
    bool error(const QString& text) { return QMetaObject::invokeMethod(&agent,"handleError",Qt::DirectConnection,Q_ARG(QString,text)); }
};
QJsonObject sphere(double radius=2)
{
    return {{"type","Sphere"},{"parameters",QJsonObject{{"radius",radius}}}};
}
QJsonArray toolResults(const Request& request)
{
    QJsonArray results;
    for (const auto& entry : request.messages) {
        const auto message=entry.toObject();
        if (message.value("role").toString()=="tool")
            results.append(QJsonDocument::fromJson(message.value("content").toString().toUtf8()).object());
    }
    return results;
}
QJsonObject automaticAnalysis(const Request& request)
{
    for (const auto& item : request.messages) {
        const auto message=item.toObject();
        const auto content=message.value("content").toString();
        if (message.value("role")=="system" && content.startsWith(QStringLiteral("本机已用 analyze_geometry")))
            return QJsonDocument::fromJson(content.section('\n',1,1).toUtf8()).object();
    }
    return {};
}
}

TEST(AgentLoopTest, AutomaticAnalysisPrecedesFinalResponseAndRefreshesAfterRepair)
{
    ensureCoreApplication(); ScriptedSession session;
    session.agent.submit(QStringLiteral("建一个一体模型"));
    ASSERT_TRUE(automaticAnalysis(session.requests.back()).isEmpty());
    ASSERT_TRUE(session.tools({session.call("create_feature",sphere()),
        session.call("create_feature",{{"type","Sphere"},{"parameters",QJsonObject{{"radius",2},{"x",10}}}}),
        session.call("create_boolean",{{"operation","union"},{"base_id","Sphere001"},{"tool_id","Sphere002"}})}));
    auto analysis=automaticAnalysis(session.requests.back());
    ASSERT_TRUE(analysis.value("success").toBool());
    EXPECT_EQ(analysis.value("feature_ids").toArray(),(QJsonArray{"Union001"}));
    EXPECT_EQ(analysis.value("geometry").toObject().value("solid_count").toInt(),2);
    EXPECT_FALSE(analysis.value("geometry").toObject().value("single_solid").toBool());
    // The model can use the automatic feedback to repair before issuing a final answer.
    ASSERT_TRUE(session.tools({session.call("set_position",{{"feature_id","Sphere002"},{"x",3},{"y",0},{"z",0}})}));
    analysis=automaticAnalysis(session.requests.back());
    EXPECT_EQ(analysis.value("geometry").toObject().value("solid_count").toInt(),1);
    EXPECT_TRUE(analysis.value("geometry").toObject().value("single_solid").toBool());
    ASSERT_TRUE(session.text(QStringLiteral("已修正连接。")));
    EXPECT_FALSE(session.agent.isBusy());
    EXPECT_TRUE(session.answers.last().contains(QStringLiteral("最终结果含 1 个实体")));
    session.agent.submit(QStringLiteral("查询现有对象"));
    EXPECT_TRUE(automaticAnalysis(session.requests.back()).isEmpty());
    for (const auto& item : session.requests.back().messages)
        if (item.toObject().value("role")=="system")
            EXPECT_FALSE(item.toObject().value("content").toString().startsWith(QStringLiteral("本机已用 analyze_geometry")));
    ASSERT_TRUE(session.tools({session.call("list_features")}));
    EXPECT_TRUE(automaticAnalysis(session.requests.back()).isEmpty());
    ASSERT_TRUE(session.text(QStringLiteral("当前有Union001。")));
}

TEST(AgentLoopTest, LocalFinalReportCannotHideMultipleSolidsOrFailedGeometry)
{
    ensureCoreApplication(); ScriptedSession session;
    session.agent.submit("test");
    ASSERT_TRUE(session.tools({session.call("create_feature",sphere()),
        session.call("create_feature",{{"type","Sphere"},{"parameters",QJsonObject{{"radius",2},{"x",10}}}})}));
    ASSERT_TRUE(session.text(QStringLiteral("模型已完成。")));
    EXPECT_TRUE(session.answers.last().contains(QStringLiteral("最终结果含 2 个实体")));
    EXPECT_TRUE(session.answers.last().contains(QStringLiteral("尚不能称为单一实体")));
    ScriptedSession failing;
    failing.agent.submit("test");
    ASSERT_TRUE(failing.tools({failing.call("create_feature",{{"type","CircleSketch"},{"parameters",QJsonObject{{"radius",2},{"x",10}}}}),
        failing.call("create_revolve",{{"sketch_id","CircleSketch001"},{"angle",360}}),
        failing.call("set_position",{{"feature_id","CircleSketch001"},{"x",0},{"y",0},{"z",0}})}));
    const auto analysis=automaticAnalysis(failing.requests.back());
    EXPECT_FALSE(analysis.value("success").toBool()); EXPECT_FALSE(analysis.contains("geometry"));
    ASSERT_TRUE(failing.text(QStringLiteral("需要修复草图。")));
    EXPECT_TRUE(failing.answers.last().contains(QStringLiteral("本机几何检查未通过")));
    EXPECT_EQ(failing.document.features().size(),2);
    failing.document.undo();
    EXPECT_EQ(failing.document.rebuildReport().at("Revolve001").status,forge::core::RebuildStatus::Ready);
}

TEST(AgentLoopTest, DesignExplanationIsShownBeforeMutationAndExecutionContinues)
{
    ensureCoreApplication(); ScriptedSession session;
    QStringList events;
    QObject::connect(&session.agent,&forge::assistant::AgentController::assistantMessage,
        [&events](const QString&) { events.append("explanation"); });
    QObject::connect(&session.agent,&forge::assistant::AgentController::modelChanged,
        [&events](const QString&) { events.append("mutation"); });
    session.agent.submit(QStringLiteral("自由设计，你直接来"));
    const auto plan=QStringLiteral("采用简洁球形主体，半径2毫米；当前工具没有圆角。这里开始创建主体。");
    ASSERT_TRUE(session.reply({{"role","assistant"},{"content",plan},
        {"tool_calls",QJsonArray{session.call("create_feature",sphere())}}}));
    EXPECT_EQ(events,(QStringList{"explanation","mutation"}));
    EXPECT_EQ(session.answers,(QStringList{plan}));
    EXPECT_EQ(session.document.features().size(),1);
    EXPECT_TRUE(session.agent.isBusy());
    ASSERT_EQ(session.requests.size(),2);
    int planMessages=0;
    for (const auto& entry : session.requests.back().messages)
        if (entry.toObject().value("content").toString()==plan) ++planMessages;
    EXPECT_EQ(planMessages,1); // 展示说明不额外追加历史，也不单独等待用户确认。
    ASSERT_TRUE(session.tools({session.call("get_feature",{{"feature_id","Sphere001"}})}));
    EXPECT_EQ(session.answers.size(),1); // 纯工具响应不产生空白聊天消息。
    ASSERT_TRUE(session.text(QStringLiteral("球体已创建，重建成功。")));
    EXPECT_FALSE(session.agent.isBusy());
    EXPECT_EQ(session.answers.size(),2);
}

TEST(AgentLoopTest, TrophyCompletesBeyondSixRoundsAsOneConnectedHollowSolid)
{
    ensureCoreApplication(); ScriptedSession session;
    session.agent.submit(QStringLiteral("奖杯，你直接来"));
    // 与模型服务使用相同的响应入口：真实几何执行 13 轮，旧的六轮上限会中断。
    const auto run=[&](const QString& name,const QJsonObject& arguments) {
        EXPECT_TRUE(session.agent.isBusy());
        EXPECT_TRUE(session.requests.back().allowTools);
        EXPECT_TRUE(session.tools({session.call(name,arguments)}));
        EXPECT_TRUE(session.agent.isBusy());
        const auto results=toolResults(session.requests.back());
        EXPECT_TRUE(results.last().toObject().value("success").toBool()) << results.last().toObject().value("error").toString().toStdString();
    };
    run("get_document_status",{}); run("get_feature_types",{});
    run("create_feature",{{"type","Cylinder"},{"parameters",QJsonObject{{"radius",45},{"height",15}}}});
    run("create_feature",{{"type","Cylinder"},{"parameters",QJsonObject{{"radius",32},{"height",10},{"z",15}}}});
    run("create_feature",{{"type","Cylinder"},{"parameters",QJsonObject{{"radius",8},{"height",140},{"z",25}}}});
    run("create_feature",{{"type","Sphere"},{"parameters",QJsonObject{{"radius",40},{"z",200}}}});
    run("create_feature",{{"type","Sphere"},{"parameters",QJsonObject{{"radius",34},{"z",200}}}});
    run("create_feature",{{"type","Box"},{"parameters",QJsonObject{{"length",100},{"width",100},{"height",60},{"x",-50},{"y",-50},{"z",200}}}});
    run("create_boolean",{{"operation","difference"},{"base_id","Sphere001"},{"tool_id","Box001"}});
    run("create_boolean",{{"operation","difference"},{"base_id","Cut001"},{"tool_id","Sphere002"}});
    run("create_boolean",{{"operation","union"},{"base_id","Cylinder001"},{"tool_id","Cylinder002"}});
    run("create_boolean",{{"operation","union"},{"base_id","Union001"},{"tool_id","Cylinder003"}});
    run("create_boolean",{{"operation","union"},{"base_id","Union002"},{"tool_id","Cut002"}});
    ASSERT_TRUE(session.text(QStringLiteral("已完成台阶底座、细立柱和开口半球杯身，总高200毫米。")));
    EXPECT_FALSE(session.agent.isBusy()); EXPECT_TRUE(session.errors.isEmpty());
    EXPECT_EQ(session.requests.size(),14);
    EXPECT_EQ(session.document.visibleFeatureIds(),(std::vector<std::string>{"Union003"}));
    const auto shape=session.document.rebuildReport().at("Union003").shape;
    ASSERT_FALSE(shape.IsNull()); EXPECT_TRUE(BRepCheck_Analyzer(shape).IsValid());
    int solids=0;
    for (TopExp_Explorer explorer(shape,TopAbs_SOLID);explorer.More();explorer.Next()) ++solids;
    EXPECT_EQ(solids,1);
    Bnd_Box bounds; BRepBndLib::AddOptimal(shape,bounds,false,false);
    double xmin,ymin,zmin,xmax,ymax,zmax; bounds.Get(xmin,ymin,zmin,xmax,ymax,zmax);
    EXPECT_NEAR(zmin,0,1e-6); EXPECT_NEAR(zmax,200,1e-6);
    EXPECT_EQ(BRepClass3d_SolidClassifier(shape,gp_Pnt(0,0,190),1e-6).State(),TopAbs_OUT); // 杯腔。
    EXPECT_EQ(BRepClass3d_SolidClassifier(shape,gp_Pnt(36,0,190),1e-6).State(),TopAbs_IN); // 杯壁。
    EXPECT_EQ(BRepClass3d_SolidClassifier(shape,gp_Pnt(0,0,2),1e-6).State(),TopAbs_IN); // 底座。
    QTemporaryDir dir; ASSERT_TRUE(dir.isValid());
    const auto path=dir.filePath("trophy.forgecad");
    forge::infrastructure::NativeDocumentIO::save(session.document.exportData(),path);
    auto restored=forge::infrastructure::NativeDocumentIO::prepare(path);
    EXPECT_EQ(restored->rebuildReport().at("Union003").status,forge::core::RebuildStatus::Ready);
}

TEST(AgentLoopTest, RoundBudgetAllowsFinalSummaryAndContinuationWithoutPermanentRestriction)
{
    ensureCoreApplication(); ScriptedSession session;
    session.agent.submit(QStringLiteral("创建球体"));
    ASSERT_TRUE(session.tools({session.call("create_feature",sphere())}));
    for (int i=1;i<24;++i) ASSERT_TRUE(session.tools({session.call("list_features")}));
    ASSERT_TRUE(session.agent.isBusy()); ASSERT_FALSE(session.requests.back().allowTools);
    EXPECT_EQ(session.requests.size(),25);
    ASSERT_TRUE(session.text(QStringLiteral("球体已创建，剩余造型尚未完成。")));
    EXPECT_FALSE(session.agent.isBusy()); EXPECT_TRUE(session.errors.isEmpty());
    ASSERT_FALSE(session.answers.isEmpty());
    EXPECT_TRUE(session.answers.last().contains("24"));
    EXPECT_TRUE(session.answers.last().contains("Sphere001"));
    EXPECT_EQ(session.document.features().size(),1);
    session.agent.submit(QStringLiteral("继续，先查询已有对象"));
    ASSERT_TRUE(session.requests.back().allowTools);
    for (const auto& entry : session.requests.back().messages)
        if (entry.toObject().value("role").toString()=="system")
            EXPECT_FALSE(entry.toObject().value("content").toString().contains(QStringLiteral("本轮仅汇报")));
    ASSERT_TRUE(session.tools({session.call("list_features")}));
    EXPECT_EQ(session.document.features().size(),1);
    ASSERT_TRUE(session.text(QStringLiteral("已有Sphere001，不重复创建。")));
}

TEST(AgentLoopTest, SummaryCannotExecuteToolsEvenIfServiceIgnoresToolChoice)
{
    ensureCoreApplication(); ScriptedSession session;
    session.agent.submit("test");
    for (int i=0;i<24;++i) ASSERT_TRUE(session.tools({session.call("list_features")}));
    ASSERT_FALSE(session.requests.back().allowTools);
    ASSERT_TRUE(session.tools({session.call("create_feature",sphere())}));
    EXPECT_TRUE(session.document.features().empty()); EXPECT_FALSE(session.agent.isBusy());
    EXPECT_EQ(session.requests.size(),25); EXPECT_FALSE(session.answers.isEmpty());
}

TEST(AgentLoopTest, CallBudgetSkipsRemainderAndPairsEveryToolCall)
{
    ensureCoreApplication(); ScriptedSession session;
    session.agent.submit("test");
    QJsonArray calls;
    for (int i=0;i<96;++i) calls.append(session.call("list_features"));
    calls.append(session.call("create_feature",sphere()));
    ASSERT_TRUE(session.tools(calls));
    ASSERT_FALSE(session.requests.back().allowTools); EXPECT_TRUE(session.document.features().empty());
    const auto results=toolResults(session.requests.back()); ASSERT_EQ(results.size(),97);
    EXPECT_TRUE(results.last().toObject().value("skipped").toBool());
    EXPECT_FALSE(results.last().toObject().value("success").toBool());
    ASSERT_TRUE(session.text("本轮已停止。")); EXPECT_TRUE(session.answers.last().contains("96"));
}

TEST(AgentLoopTest, ThreeConsecutiveFailuresStopBatchButSuccessfulRepairResetsCounter)
{
    ensureCoreApplication(); ScriptedSession session;
    session.agent.submit("test");
    ASSERT_TRUE(session.tools({session.call("unknown"),session.call("unknown"),session.call("unknown"),session.call("create_feature",sphere())}));
    ASSERT_FALSE(session.requests.back().allowTools); EXPECT_TRUE(session.document.features().empty());
    EXPECT_TRUE(toolResults(session.requests.back()).last().toObject().value("skipped").toBool());
    ASSERT_TRUE(session.text("需要修正工具名称。"));
    session.agent.submit("修复后继续");
    ASSERT_TRUE(session.tools({session.call("unknown")}));
    ASSERT_TRUE(session.tools({session.call("list_features")}));
    ASSERT_TRUE(session.tools({session.call("unknown")}));
    ASSERT_TRUE(session.tools({session.call("create_feature",sphere())}));
    EXPECT_TRUE(session.requests.back().allowTools); EXPECT_EQ(session.document.features().size(),1);
    ASSERT_TRUE(session.text("已修复并创建球体。"));
}

TEST(AgentLoopTest, CancelAndPendingInputPreventLaterCallsInSameBatch)
{
    ensureCoreApplication();
    for (const bool pending : {false,true}) {
        ScriptedSession session;
        session.agent.setUiToolHandler([pending](const QString&,const QJsonObject&) -> QJsonObject {
            if (pending) return {{"success",true},{"pending_user_input",true}};
            return {{"success",false},{"cancelled",true},{"error","用户取消"}};
        });
        session.agent.submit("test");
        ASSERT_TRUE(session.tools({session.call("create_feature",sphere())}));
        ASSERT_TRUE(session.tools({session.call(pending ? "begin_point_placement" : "new_document",pending ? QJsonObject{{"feature_id","Sphere001"}} : QJsonObject{}),session.call("create_feature",sphere(3))}));
        ASSERT_FALSE(session.requests.back().allowTools); EXPECT_EQ(session.document.features().size(),1);
        EXPECT_TRUE(toolResults(session.requests.back()).last().toObject().value("skipped").toBool());
        ASSERT_TRUE(session.text(pending ? "等待用户点击。" : "已取消后续操作。"));
        EXPECT_FALSE(session.agent.isBusy());
    }
}

TEST(AgentLoopTest, NetworkFailureAndSummaryFailureReportLocalProgressAndKeepHistory)
{
    ensureCoreApplication(); ScriptedSession session;
    session.agent.submit("test");
    ASSERT_TRUE(session.tools({session.call("create_feature",sphere())}));
    ASSERT_TRUE(session.error("模拟网络断开"));
    EXPECT_FALSE(session.agent.isBusy()); EXPECT_EQ(session.document.features().size(),1);
    EXPECT_TRUE(session.answers.last().contains("Sphere001"));
    session.agent.submit("continue");
    for (int i=0;i<24;++i) ASSERT_TRUE(session.tools({session.call("list_features")}));
    ASSERT_FALSE(session.requests.back().allowTools);
    ASSERT_TRUE(session.error("模拟收尾网络断开"));
    EXPECT_FALSE(session.agent.isBusy()); EXPECT_EQ(session.document.features().size(),1);
    EXPECT_TRUE(session.answers.last().contains("24")); EXPECT_FALSE(session.errors.isEmpty());
    session.document.undo(); EXPECT_TRUE(session.document.features().empty());
}
