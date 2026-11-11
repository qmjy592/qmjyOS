from pathlib import Path
import shutil
from datetime import datetime

ROOT = Path(__file__).resolve().parents[1]

source_path = ROOT / "init/startup/qm_init.c"
test_path = ROOT / "tests/services/test_qm_init.c"

source = source_path.read_text(encoding="utf-8")
tests = test_path.read_text(encoding="utf-8")

if "QM_INIT_STABLE_PRIORITY_FIX" in source:
    raise SystemExit("[STOP] Init Manager 补丁已经应用")

start = source.find("int qm_init_run_stage(")
end = source.find("int qm_init_run_all(", start)

if start < 0 or end < 0:
    raise SystemExit("[STOP] 原始函数结构不符合预期")

new_function = r'''/* QM_INIT_STABLE_PRIORITY_FIX */
int qm_init_run_stage(qm_init_stage_t stage)
{
    size_t completed = 0;
    size_t stage_entries = 0;
    size_t i;
    int selected;
    int callback_result;

    if (!qm_init_valid_stage(stage))
        return QM_INIT_ERR_INVALID;

    if (g_running)
        return QM_INIT_ERR_STATE;

    if (stage != g_next_stage)
        return QM_INIT_ERR_STAGE_ORDER;

    for (i = 0; i < g_count; ++i) {
        if (g_entries[i].stage != stage)
            continue;

        if (g_entries[i].state != QM_INIT_STATE_PENDING)
            return QM_INIT_ERR_STATE;

        ++stage_entries;
    }

    g_running = 1;

    while (completed < stage_entries) {
        selected = -1;

        /*
         * 每轮都扫描全部待执行项目。
         * 相同优先级保持注册顺序。
         */
        for (i = 0; i < g_count; ++i) {
            qm_init_entry_t *candidate = &g_entries[i];

            if (candidate->stage != stage ||
                candidate->state != QM_INIT_STATE_PENDING)
                continue;

            if (selected < 0 ||
                candidate->priority <
                    g_entries[selected].priority)
                selected = (int)i;
        }

        if (selected < 0) {
            g_running = 0;
            return QM_INIT_ERR_STATE;
        }

        g_entries[selected].state = QM_INIT_STATE_RUNNING;
        callback_result = g_entries[selected].callback();
        g_entries[selected].callback_result = callback_result;

        if (callback_result != 0) {
            g_entries[selected].state = QM_INIT_STATE_FAILED;
            g_running = 0;
            return QM_INIT_ERR_CALLBACK;
        }

        g_entries[selected].state = QM_INIT_STATE_DONE;
        ++completed;
    }

    g_running = 0;
    g_next_stage = (qm_init_stage_t)(stage + 1);

    return QM_INIT_OK;
}

'''

new_tests = r'''
static int callback_priority_5a(void)
{
    execution_order[execution_count++] = 51;
    return 0;
}

static int callback_priority_1(void)
{
    execution_order[execution_count++] = 1;
    return 0;
}

static int callback_priority_10(void)
{
    execution_order[execution_count++] = 10;
    return 0;
}

static int callback_priority_5b(void)
{
    execution_order[execution_count++] = 52;
    return 0;
}

static void test_priority_order(void)
{
    qm_init_init();
    execution_count = 0;

    assert(qm_init_register(
        "priority-5-a", QM_INIT_STAGE_EARLY, 5,
        callback_priority_5a) == QM_INIT_OK);

    assert(qm_init_register(
        "priority-1", QM_INIT_STAGE_EARLY, 1,
        callback_priority_1) == QM_INIT_OK);

    assert(qm_init_register(
        "priority-10", QM_INIT_STAGE_EARLY, 10,
        callback_priority_10) == QM_INIT_OK);

    assert(qm_init_register(
        "priority-5-b", QM_INIT_STAGE_EARLY, 5,
        callback_priority_5b) == QM_INIT_OK);

    assert(qm_init_run_stage(QM_INIT_STAGE_EARLY) == QM_INIT_OK);

    assert(execution_count == 4);
    assert(execution_order[0] == 1);
    assert(execution_order[1] == 51);
    assert(execution_order[2] == 52);
    assert(execution_order[3] == 10);
}

'''

if "test_priority_order" in tests:
    raise SystemExit("[STOP] 优先级回归测试已经存在")

main_marker = "int main(void)\n{"
if tests.count(main_marker) != 1:
    raise SystemExit("[STOP] 测试入口结构不符合预期")

new_source = source[:start] + new_function + source[end:]
new_test_file = tests.replace(
    main_marker,
    new_tests + main_marker,
    1
).replace(
    "int main(void)\n{\n    test_normal_initialization();",
    "int main(void)\n{\n    test_priority_order();\n    test_normal_initialization();",
    1
)

if new_test_file == tests:
    raise SystemExit("[STOP] 测试入口修改失败")

backup_dir = ROOT / "build/phase2-backups"
backup_dir.mkdir(parents=True, exist_ok=True)
stamp = datetime.now().strftime("%Y%m%d-%H%M%S")

shutil.copy2(
    source_path,
    backup_dir / f"qm_init.c.{stamp}.bak"
)
shutil.copy2(
    test_path,
    backup_dir / f"test_qm_init.c.{stamp}.bak"
)

source_path.write_text(new_source, encoding="utf-8")
test_path.write_text(new_test_file, encoding="utf-8")

print("[PASS] Init Manager stable priority selection")
print("[PASS] Stable ordering for equal priorities")
print("[PASS] Regression test added: 5, 1, 10, 5")
print("[BACKUP] build/phase2-backups/")
print("[DONE] Phase 2 Init Manager patch applied")
