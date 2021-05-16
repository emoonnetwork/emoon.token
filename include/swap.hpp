#include <token.hpp>

#define SWAP_TRADE_FEE 30

struct box_token {
    name contract;
    symbol symbol;
};

struct box_pair {
    uint64_t id;
    box_token token0;
    box_token token1;
    asset reserve0;
    asset reserve1;
    uint64_t liquidity_token;
    double price0_last;
    double price1_last;
    uint64_t price0_cumulative_last;
    uint64_t price1_cumulative_last;
    time_point_sec block_time_last;

    uint64_t primary_key() const { return id; }
};

typedef multi_index<"pairs"_n, box_pair> box_pairs;

uint64_t get_output_amount(uint64_t input_amount, uint64_t input_reserve, uint64_t output_reserve) {
    if (input_amount <= 0 || input_reserve <= 0 ||output_reserve <= 0) {
        return 0;
    }
    uint64_t trade_fee = (uint128_t)input_amount *  SWAP_TRADE_FEE / 10000;
    uint128_t input_amount_with_fee = input_amount - trade_fee;
    uint128_t numerator = input_amount_with_fee * output_reserve;
    uint128_t denominator = input_reserve + input_amount_with_fee;
    return numerator / denominator;
}

uint64_t get_eos_value(asset em_quantity) {
    box_pairs pairs(name("swap.defi"), name("swap.defi").value);
    auto p = pairs.require_find(token::PAIR_ID);
    auto input = p->reserve0.symbol == em_quantity.symbol ? p->reserve0.amount : p->reserve1.amount;
    auto output = p->reserve0.symbol == em_quantity.symbol ? p->reserve1.amount : p->reserve0.amount;
    return get_output_amount(em_quantity.amount, input, output);
}