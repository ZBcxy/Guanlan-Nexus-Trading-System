# 观澜 · Nexus | Guanlan Nexus Trading System（GNTS）

## 项目简介

### 观澜 · Nexus | Guanlan Nexus Trading System（GNTS）是一个用C语言开发的量化交易系统，包含五大核心模块：

1. **数据系统** - 行情数据、财务数据、数据存储与管理
2. **策略研究平台** - 因子挖掘、策略编写、参数优化
3. **回测引擎** - 逐笔回测、手续费模拟、绩效评估
4. **交易执行系统（OMS）** - 订单管理、仓位管理、算法交易
5. **风险控制与监控系统** - 事前风控、事中监控、事后分析

## 项目结构

```
GNTS/
├── include/              # 头文件目录
│   ├── common/          # 公共定义
│   │   ├── types.h      # 基础类型定义
│   │   ├── log.h        # 日志系统
│   │   ├── structures.h # 数据结构定义
│   │   ├── memory_pool.h # 内存池
│   │   ├── lockfree_queue.h # 无锁队列
│   │   └── high_performance.h # 高性能数据结构
│   ├── data_system/     # 数据系统
│   │   └── data_manager.h
│   ├── strategy/        # 策略研究平台
│   │   └── strategy_engine.h
│   ├── backtest/        # 回测引擎
│   │   └── backtest_engine.h
│   ├── oms/             # 交易执行系统
│   │   └── order_manager.h
│   ├── risk/            # 风控监控系统
│   │   └── risk_manager.h
│   └── gnts.h           # 主头文件
├── src/                 # 源文件目录
│   ├── common/
│   │   ├── gnts.c, log.c, memory_pool.c
│   │   ├── lockfree_queue.c, high_performance.c
│   ├── data_system/
│   │   ├── data_manager.c, data_cleaner.c
│   │   └── memory_storage.c
│   ├── strategy/
│   │   ├── strategy_engine.c, factor.c, optimizer.c
│   ├── backtest/
│   │   ├── backtest_engine.c, backtest_report.c
│   │   └── performance_analyzer.c
│   ├── oms/
│   │   ├── order_manager.c, position_manager.c
│   │   └── algo_trader.c
│   └── risk/
│       ├── risk_manager.c, monitor_system.c
│       └── analyzer.c
├── tests/               # 测试目录
├── docs/                # 文档目录
├── CMakeLists.txt       # CMake构建文件
├── Makefile             # GNU Make构建文件
└── README.md            # 项目说明
```

## 核心模块说明

### 1. 数据系统（Data System）

负责数据的获取、存储和管理：

- **数据源接口**：支持多种数据源（AKShare、Tushare、XTP、CTP等）
- **数据存储**：支持内存、SQLite、InfluxDB、ClickHouse等存储方式
- **数据清洗**：处理停牌、除权除息、异常数据
- **实时推送**：支持Tick和K线数据的实时推送

### 2. 策略研究平台（Strategy Research Platform）

提供策略开发和优化环境：

- **因子挖掘**：技术因子、基本面因子、情绪因子、自定义因子
- **策略编写**：标准化的策略接口，支持生命周期管理
- **参数优化**：网格搜索、遗传算法、随机搜索、贝叶斯优化

### 3. 回测引擎（Backtest Engine）

模拟历史行情，检验策略表现：

- **逐笔回测**：Bar-by-Bar回测，支持多品种、多周期
- **成本模拟**：手续费、滑点、印花税、过户费
- **绩效评估**：夏普比率、最大回撤、胜率、盈亏比等指标
- **回测报告**：生成HTML和JSON格式的详细报告

### 4. 交易执行系统（Trade execution system）

订单管理与执行：

- **券商接口**：支持XTP、CTP等主流券商接口
- **订单管理**：订单发送、撤单、查询
- **仓位管理**：持仓查询、账户管理
- **算法交易**：TWAP、VWAP、冰山指令、狙击手等算法

### 5. 风险控制与监控系统（Risk Management）

独立的风控和监控模块：

- **事前风控**：持仓限制、订单限制、亏损限制、黑名单等
- **事中监控**：实时监控交易延迟、连接状态、系统指标
- **事后分析**：盈亏归因、交易分析、滑点分析

## 编译与安装

### 编译

```bash
make
```

### 测试

```bash
make test
```

### 安装

```bash
sudo make install
```

### 卸载

```bash
sudo make uninstall
```

## 使用示例

### 1. 初始化系统

```c
#include <gnts.h>

int main() {
    // 初始化GNTS系统
    if (gnts_init() != GNTS_OK) {
        fprintf(stderr, "Failed to initialize GNTS\n");
        return -1;
    }

    // ... 使用各模块 ...

    // 关闭系统
    gnts_shutdown();
    return 0;
}
```

### 2. 数据系统示例

```c
// 创建数据管理器
data_manager_t* dm = data_manager_create();

// 添加数据源
data_source_interface_t* source = create_xtp_source();
data_manager_add_source(dm, source);

// 订阅行情
data_manager_subscribe(dm, "600000.SH");

// 查询历史数据
bar_data_t* bars;
int count;
data_manager_query_bars(dm, "600000.SH", start_time, end_time,
                         BAR_PERIOD_1DAY, &bars, &count);

// 销毁
data_manager_destroy(dm);
```

### 3. 策略示例

```c
// 定义策略
typedef struct {
    strategy_interface_t base;
    int fast_period;
    int slow_period;
} ma_strategy_t;

// 实现策略逻辑
signal_t generate_signal(strategy_interface_t* self, const char* symbol) {
    ma_strategy_t* strategy = (ma_strategy_t*)self;
    // ... 计算均线，生成信号 ...
    return SIGNAL_BUY;
}

// 创建并运行策略
ma_strategy_t strategy;
strategy.base.generate_signal = generate_signal;
strategy.fast_period = 5;
strategy.slow_period = 20;

strategy_engine_t* engine = strategy_engine_create();
strategy_engine_add_strategy(engine, &strategy.base);
strategy_engine_start(engine);
```

### 4. 回测示例

```c
// 配置回测参数
backtest_config_t config = {
    .start_time = 1609459200000000000LL,  // 2021-01-01
    .end_time = 1640995200000000000LL,    // 2021-12-31
    .initial_capital = 1000000.0,
    .commission = {.type = COMMISSION_TYPE_RATIO, .value = 0.0003},
    .slippage = {.type = SLIPPAGE_TYPE_RATIO, .value = 0.0001}
};

// 创建回测引擎
backtest_engine_t* engine = backtest_engine_create(&config);

// 加载历史数据
backtest_engine_load_data(engine, "600000.SH", bars, bar_count);

// 设置策略
backtest_engine_set_strategy(engine, &strategy.base);

// 运行回测
backtest_engine_run(engine);

// 获取结果
backtest_statistics_t stats;
backtest_engine_get_statistics(engine, &stats);

printf("Total Return: %.2f%%\n", stats.total_return * 100);
printf("Max Drawdown: %.2f%%\n", stats.max_drawdown * 100);
printf("Sharpe Ratio: %.2f\n", stats.sharpe_ratio);
```

## 开发计划

### 已完成 ✅
- [x] 架构设计与接口定义
- [x] 数据系统实现（含数据清洗、内存存储）
- [x] 策略研究平台实现（含因子计算、参数优化框架）
- [x] 回测引擎实现（含成本模拟、报告生成）
- [x] 交易执行系统实现（含算法交易：TWAP/VWAP/冰山）
- [x] 风控监控系统实现（含事前/事中/事后）
- [x] 高性能组件（内存池、无锁队列、分段锁哈希表、滑动窗口）

### 进行中 🚧
- [ ] 单元测试完善
- [ ] 对接真实数据源（AKShare、Tushare）
- [ ] 对接券商API（XTP、CTP）
- [ ] 参数优化器完整实现（网格搜索/遗传/贝叶斯）

### 待开发 📋
- [ ] Web界面管理
- [ ] 数据可视化
- [ ] 策略回测报告Web展示
- [ ] 实盘交易对接
- [ ] 多账户管理
- [ ] 分布式部署支持

## 技术特点

- **高性能**：C语言实现，低延迟、高吞吐
- **模块化**：各模块独立，接口清晰，易于扩展
- **可定制**：支持自定义数据源、策略、风控规则
- **跨平台**：支持Linux、Windows、macOS

## 许可证

MIT License

## 联系方式

项目主页：<https://github.com/ZBcxy/GNTS.git>
问题反馈：<954510662@qq.com>
