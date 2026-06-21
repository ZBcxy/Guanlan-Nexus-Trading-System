/*
 * GNTS - 观澜 · Nexus Trading System
 * 回测报告生成工具实现
 */

#include "backtest/backtest_engine.h"
#include "common/log.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* 创建回测报告 */
backtest_report_t* backtest_report_create(backtest_engine_t* engine) {
    if (engine == NULL) {
        LOG_ERROR("Invalid engine for report creation");
        return NULL;
    }

    backtest_report_t* report = (backtest_report_t*)malloc(sizeof(backtest_report_t));
    if (report == NULL) {
        LOG_ERROR("Failed to allocate memory for report");
        return NULL;
    }

    /* 获取统计数据 */
    backtest_engine_get_statistics(engine, &report->statistics);

    /* 获取资金曲线 */
    backtest_engine_get_equity_curve(engine, &report->equity_curve, &report->equity_count);

    /* 获取交易记录 */
    backtest_engine_get_trades(engine, &report->trades, &report->trade_count);

    /* 获取订单记录 */
    backtest_engine_get_orders(engine, &report->orders, &report->order_count);

    report->report_html = NULL;
    report->report_json = NULL;

    LOG_INFO("Backtest report created");
    return report;
}

/* 销毁回测报告 */
void backtest_report_destroy(backtest_report_t* report) {
    if (report == NULL) {
        return;
    }

    if (report->report_html != NULL) {
        free(report->report_html);
    }

    if (report->report_json != NULL) {
        free(report->report_json);
    }

    free(report);
    LOG_INFO("Backtest report destroyed");
}

/* 生成HTML报告 */
int backtest_report_generate_html(backtest_report_t* report, const char* filename) {
    if (report == NULL || filename == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    FILE* file = fopen(filename, "w");
    if (file == NULL) {
        LOG_ERROR("Failed to open file %s", filename);
        return GNTS_ERROR;
    }

    /* 生成HTML内容 */
    fprintf(file, "<html>\n");
    fprintf(file, "<head>\n");
    fprintf(file, "<title>GNTS Backtest Report</title>\n");
    fprintf(file, "<style>\n");
    fprintf(file, "body { font-family: Arial, sans-serif; margin: 20px; }\n");
    fprintf(file, "table { border-collapse: collapse; width: 100%; }\n");
    fprintf(file, "th, td { border: 1px solid #ddd; padding: 8px; text-align: left; }\n");
    fprintf(file, "th { background-color: #4CAF50; color: white; }\n");
    fprintf(file, ".positive { color: green; }\n");
    fprintf(file, ".negative { color: red; }\n");
    fprintf(file, "</style>\n");
    fprintf(file, "</head>\n");
    fprintf(file, "<body>\n");

    /* 标题 */
    fprintf(file, "<h1>GNTS Backtest Report</h1>\n");

    /* 统计数据 */
    fprintf(file, "<h2>Performance Statistics</h2>\n");
    fprintf(file, "<table>\n");
    fprintf(file, "<tr><th>Metric</th><th>Value</th></tr>\n");
    fprintf(file, "<tr><td>Total Return</td><td>%.2f%%</td></tr>\n", report->statistics.total_return * 100);
    fprintf(file, "<tr><td>Annual Return</td><td>%.2f%%</td></tr>\n", report->statistics.annual_return * 100);
    fprintf(file, "<tr><td>Max Drawdown</td><td>%.2f%%</td></tr>\n", report->statistics.max_drawdown * 100);
    fprintf(file, "<tr><td>Sharpe Ratio</td><td>%.2f</td></tr>\n", report->statistics.sharpe_ratio);
    fprintf(file, "<tr><td>Sortino Ratio</td><td>%.2f</td></tr>\n", report->statistics.sortino_ratio);
    fprintf(file, "<tr><td>Win Rate</td><td>%.2f%%</td></tr>\n", report->statistics.win_rate * 100);
    fprintf(file, "<tr><td>Total Trades</td><td>%lld</td></tr>\n", report->statistics.total_trades);
    fprintf(file, "<tr><td>Win Trades</td><td>%lld</td></tr>\n", report->statistics.win_trades);
    fprintf(file, "<tr><td>Loss Trades</td><td>%lld</td></tr>\n", report->statistics.loss_trades);
    fprintf(file, "</table>\n");

    /* 交易记录 */
    fprintf(file, "<h2>Trade Records</h2>\n");
    fprintf(file, "<table>\n");
    fprintf(file, "<tr><th>Trade ID</th><th>Symbol</th><th>Direction</th><th>Price</th><th>Quantity</th><th>Time</th></tr>\n");

    for (int i = 0; i < report->trade_count; i++) {
        const trade_record_t* trade = &report->trades[i];
        fprintf(file, "<tr><td>%s</td><td>%s</td><td>%s</td><td>%.2f</td><td>%lld</td><td>%lld</td></tr>\n",
                trade->trade_id, trade->symbol,
                (trade->direction == ORDER_BUY) ? "BUY" : "SELL",
                trade->price, trade->quantity, trade->trade_time);
    }

    fprintf(file, "</table>\n");

    /* 资金曲线 */
    fprintf(file, "<h2>Equity Curve</h2>\n");
    fprintf(file, "<table>\n");
    fprintf(file, "<tr><th>Day</th><th>Equity</th></tr>\n");

    for (int i = 0; i < report->equity_count; i++) {
        fprintf(file, "<tr><td>%d</td><td>%.2f</td></tr>\n", i, report->equity_curve[i]);
    }

    fprintf(file, "</table>\n");

    fprintf(file, "</body>\n");
    fprintf(file, "</html>\n");

    fclose(file);

    LOG_INFO("HTML report generated: %s", filename);
    return GNTS_OK;
}

/* 生成JSON报告 */
int backtest_report_generate_json(backtest_report_t* report, const char* filename) {
    if (report == NULL || filename == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    FILE* file = fopen(filename, "w");
    if (file == NULL) {
        LOG_ERROR("Failed to open file %s", filename);
        return GNTS_ERROR;
    }

    /* 生成JSON内容 */
    fprintf(file, "{\n");

    /* 统计数据 */
    fprintf(file, "  \"statistics\": {\n");
    fprintf(file, "    \"total_return\": %.4f,\n", report->statistics.total_return);
    fprintf(file, "    \"annual_return\": %.4f,\n", report->statistics.annual_return);
    fprintf(file, "    \"max_drawdown\": %.4f,\n", report->statistics.max_drawdown);
    fprintf(file, "    \"sharpe_ratio\": %.4f,\n", report->statistics.sharpe_ratio);
    fprintf(file, "    \"sortino_ratio\": %.4f,\n", report->statistics.sortino_ratio);
    fprintf(file, "    \"win_rate\": %.4f,\n", report->statistics.win_rate);
    fprintf(file, "    \"total_trades\": %lld,\n", report->statistics.total_trades);
    fprintf(file, "    \"win_trades\": %lld,\n", report->statistics.win_trades);
    fprintf(file, "    \"loss_trades\": %lld\n", report->statistics.loss_trades);
    fprintf(file, "  },\n");

    /* 交易记录 */
    fprintf(file, "  \"trades\": [\n");

    for (int i = 0; i < report->trade_count; i++) {
        const trade_record_t* trade = &report->trades[i];
        fprintf(file, "    {\n");
        fprintf(file, "      \"trade_id\": \"%s\",\n", trade->trade_id);
        fprintf(file, "      \"symbol\": \"%s\",\n", trade->symbol);
        fprintf(file, "      \"direction\": \"%s\",\n", (trade->direction == ORDER_BUY) ? "BUY" : "SELL");
        fprintf(file, "      \"price\": %.2f,\n", trade->price);
        fprintf(file, "      \"quantity\": %lld,\n", trade->quantity);
        fprintf(file, "      \"trade_time\": %lld\n", trade->trade_time);
        fprintf(file, "    }%s\n", (i < report->trade_count - 1) ? "," : "");
    }

    fprintf(file, "  ],\n");

    /* 资金曲线 */
    fprintf(file, "  \"equity_curve\": [\n");

    for (int i = 0; i < report->equity_count; i++) {
        fprintf(file, "    %.2f%s\n", report->equity_curve[i], (i < report->equity_count - 1) ? "," : "");
    }

    fprintf(file, "  ]\n");

    fprintf(file, "}\n");

    fclose(file);

    LOG_INFO("JSON report generated: %s", filename);
    return GNTS_OK;
}