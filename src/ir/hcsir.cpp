#include "ir/hcsir.h"

#include "core/internal.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

namespace hcs::ir {
namespace {

bool isIdentifier(const std::string& text)
{
    if (text.empty() || text.size() > kMaxIdentifier) {
        return false;
    }
    const unsigned char first = static_cast<unsigned char>(text[0]);
    if (!(std::isalpha(first) || first == '_')) {
        return false;
    }
    for (char c : text) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (!(std::isalnum(u) || u == '_' || u == '.')) {
            return false;
        }
    }
    return true;
}

std::string trim(const std::string& text)
{
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return text.substr(begin, end - begin);
}

std::vector<std::string> tokenize(const std::string& line)
{
    std::vector<std::string> tokens;
    std::string current;
    for (char c : line) {
        if (std::isspace(static_cast<unsigned char>(c)) || c == ',') {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        tokens.push_back(current);
    }
    return tokens;
}

bool parseUnsigned(const std::string& text, uint64_t* out)
{
    if (text.empty()) {
        return false;
    }
    uint64_t value = 0;
    for (char c : text) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            return false;
        }
        value = value * 10 + static_cast<uint64_t>(c - '0');
        if (value > 0xFFFFFFFFull) {
            return false;
        }
    }
    *out = value;
    return true;
}

bool parseNumber(const std::string& text, double* out, bool* isInteger)
{
    errno = 0;
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' || errno == ERANGE) {
        return false;
    }
    *out = value;
    bool integer = true;
    for (char c : text) {
        if (c == '.' || c == 'e' || c == 'E') {
            integer = false;
        }
    }
    *isInteger = integer;
    return true;
}

Type typeFromName(const std::string& name)
{
    if (name == "f32") {
        return Type::F32;
    }
    if (name == "u32") {
        return Type::U32;
    }
    if (name == "i32") {
        return Type::I32;
    }
    if (name == "bool") {
        return Type::Bool;
    }
    return Type::Void;
}

bool isNumeric(Type type)
{
    return type == Type::F32 || type == Type::U32 || type == Type::I32;
}

bool isIntegerType(Type type) { return type == Type::U32 || type == Type::I32; }

struct Parser {
    std::string source;
    Module module;
    std::vector<std::string> lines;
    std::vector<uint32_t> lineNumbers;
    std::unordered_map<std::string, uint32_t> valueIds;
    std::unordered_map<std::string, uint32_t> bufferOrdinals;
    std::unordered_map<std::string, uint32_t> blockIndex;
    std::unordered_set<std::string> globalNames;
    std::vector<std::pair<uint32_t, std::string>> branchUses; /* line, target */
    uint32_t nextValue = 1;
    uint32_t currentLine = 0;

    hcsResult error(hcsResult code, const std::string& message) const
    {
        char buffer[512];
        std::snprintf(buffer, sizeof(buffer), "HCSIR line %u: %s", currentLine,
                      message.c_str());
        return fail(code, "hcsir.compile", buffer);
    }

    hcsResult parseHeader(const std::string& line)
    {
        const std::vector<std::string> tokens = tokenize(line);
        if (tokens.size() != 2 || tokens[0] != "hcsir") {
            return error(HCS_E_INVALID_ARGUMENT, "expected 'hcsir <version>'");
        }
        uint64_t version = 0;
        if (!parseUnsigned(tokens[1], &version)) {
            return error(HCS_E_INVALID_ARGUMENT, "invalid HCSIR version");
        }
        if (version != kVersion) {
            return error(HCS_E_UNSUPPORTED_API_VERSION,
                         "unsupported HCSIR version " + tokens[1]);
        }
        return HCS_SUCCESS;
    }

    hcsResult parseBuffer(const std::vector<std::string>& tokens)
    {
        /* buffer <index> <name> : f32 */
        if (tokens.size() != 5 || tokens[3] != ":") {
            return error(HCS_E_INVALID_ARGUMENT, "expected 'buffer <n> <name> : f32'");
        }
        uint64_t index = 0;
        if (!parseUnsigned(tokens[1], &index) || index >= kMaxBuffers) {
            return error(HCS_E_INVALID_ARGUMENT, "buffer index out of range (0..7)");
        }
        if (!isIdentifier(tokens[2])) {
            return error(HCS_E_INVALID_ARGUMENT, "invalid buffer name");
        }
        if (typeFromName(tokens[4]) != Type::F32) {
            return error(HCS_E_UNSUPPORTED_FEATURE,
                         "HCSIR v1 supports only f32 storage buffers");
        }
        for (const auto& existing : module.buffers) {
            if (existing.index == index) {
                return error(HCS_E_INVALID_ARGUMENT, "duplicate buffer index");
            }
            if (existing.name == tokens[2]) {
                return error(HCS_E_INVALID_ARGUMENT, "duplicate buffer name");
            }
        }
        if (!globalNames.insert(tokens[2]).second) {
            return error(HCS_E_INVALID_ARGUMENT, "name already declared");
        }
        BufferDecl decl;
        decl.index = static_cast<uint32_t>(index);
        decl.name = tokens[2];
        decl.elemType = Type::F32;
        bufferOrdinals[decl.name] = static_cast<uint32_t>(module.buffers.size());
        module.buffers.push_back(decl);
        return HCS_SUCCESS;
    }

    hcsResult parseParam(const std::vector<std::string>& tokens)
    {
        /* param <name> : <type> */
        if (tokens.size() != 4 || tokens[2] != ":") {
            return error(HCS_E_INVALID_ARGUMENT, "expected 'param <name> : <type>'");
        }
        if (!isIdentifier(tokens[1])) {
            return error(HCS_E_INVALID_ARGUMENT, "invalid parameter name");
        }
        const Type type = typeFromName(tokens[3]);
        if (!isNumeric(type)) {
            return error(HCS_E_UNSUPPORTED_FEATURE, "parameter type must be f32, u32 or i32");
        }
        if (module.params.size() >= kMaxParams) {
            return error(HCS_E_SECURITY_POLICY, "too many parameters");
        }
        if (!globalNames.insert(tokens[1]).second) {
            return error(HCS_E_INVALID_ARGUMENT, "name already declared");
        }
        ParamDecl decl;
        decl.name = tokens[1];
        decl.type = type;
        /* Value id 0 is reserved as the invalid id; parameters start at 1. */
        decl.valueId = nextValue++;
        if (nextValue > kMaxValues) {
            return error(HCS_E_SECURITY_POLICY, "value limit exceeded");
        }
        module.valueTypes.resize(nextValue);
        module.valueTypes[decl.valueId] = type;
        valueIds[decl.name] = decl.valueId;
        module.params.push_back(decl);
        return HCS_SUCCESS;
    }

    hcsResult parseConstant(const std::vector<std::string>& tokens)
    {
        /* const <name> : <type> = <literal> */
        if (tokens.size() != 6 || tokens[2] != ":" || tokens[4] != "=") {
            return error(HCS_E_INVALID_ARGUMENT,
                         "expected 'const <name> : <type> = <literal>'");
        }
        if (!isIdentifier(tokens[1])) {
            return error(HCS_E_INVALID_ARGUMENT, "invalid constant name");
        }
        const Type type = typeFromName(tokens[3]);
        if (!isNumeric(type)) {
            return error(HCS_E_UNSUPPORTED_FEATURE,
                         "constant type must be f32, u32 or i32");
        }
        double number = 0.0;
        bool integer = false;
        if (!parseNumber(tokens[5], &number, &integer)) {
            return error(HCS_E_INVALID_ARGUMENT, "invalid constant literal");
        }
        if (type == Type::F32 && integer) {
            number = static_cast<double>(static_cast<long long>(number));
        }
        if (!globalNames.insert(tokens[1]).second) {
            return error(HCS_E_INVALID_ARGUMENT, "name already declared");
        }
        if (nextValue >= kMaxValues) {
            return error(HCS_E_SECURITY_POLICY, "value limit exceeded");
        }
        ConstantDecl decl;
        decl.name = tokens[1];
        decl.type = type;
        decl.value = number;
        decl.valueId = nextValue++;
        module.valueTypes.resize(nextValue);
        module.valueTypes[decl.valueId] = type;
        valueIds[decl.name] = decl.valueId;
        module.constants.push_back(decl);
        return HCS_SUCCESS;
    }

    hcsResult resolveOperand(const std::string& token, Operand* out, Type expectedHint)
    {
        Operand operand;
        auto it = valueIds.find(token);
        if (it != valueIds.end()) {
            operand.kind = Operand::Kind::Value;
            operand.id = it->second;
            *out = operand;
            return HCS_SUCCESS;
        }
        double number = 0.0;
        bool integer = false;
        if (parseNumber(token, &number, &integer)) {
            if (integer) {
                operand.kind = Operand::Kind::IntLit;
            } else {
                operand.kind = Operand::Kind::FloatLit;
            }
            operand.number = number;
            *out = operand;
            return HCS_SUCCESS;
        }
        (void)expectedHint;
        return error(HCS_E_INVALID_ARGUMENT, "undefined value '" + token + "'");
    }

    hcsResult operandType(const Operand& operand, Type* out) const
    {
        if (operand.kind == Operand::Kind::Value) {
            if (operand.id >= module.valueTypes.size()) {
                return HCS_E_INVALID_ARGUMENT;
            }
            *out = module.valueTypes[operand.id];
            return HCS_SUCCESS;
        }
        if (operand.kind == Operand::Kind::FloatLit) {
            *out = Type::F32;
            return HCS_SUCCESS;
        }
        if (operand.kind == Operand::Kind::IntLit) {
            *out = Type::I32;
            return HCS_SUCCESS;
        }
        return HCS_E_INVALID_ARGUMENT;
    }

    hcsResult defineValue(const std::string& name, Type type, int* idOut)
    {
        if (!isIdentifier(name)) {
            return error(HCS_E_INVALID_ARGUMENT, "invalid value name");
        }
        if (valueIds.count(name) != 0) {
            return error(HCS_E_INVALID_ARGUMENT, "value redefined: " + name);
        }
        if (nextValue >= kMaxValues) {
            return error(HCS_E_SECURITY_POLICY, "value limit exceeded");
        }
        const uint32_t id = nextValue++;
        module.valueTypes.resize(nextValue);
        module.valueTypes[id] = type;
        valueIds[name] = id;
        *idOut = static_cast<int>(id);
        return HCS_SUCCESS;
    }

    hcsResult requireType(const Operand& operand, Type expected, const char* what)
    {
        Type actual = Type::Void;
        const hcsResult r = operandType(operand, &actual);
        if (r != HCS_SUCCESS) {
            return error(HCS_E_INVALID_ARGUMENT, "invalid operand");
        }
        if (actual != expected) {
            char buffer[128];
            std::snprintf(buffer, sizeof(buffer), "%s requires %s, got %s", what,
                          typeName(expected), typeName(actual));
            return error(HCS_E_INVALID_ARGUMENT, buffer);
        }
        return HCS_SUCCESS;
    }

    hcsResult parseInstruction(const std::string& line, Block* block)
    {
        std::vector<std::string> tokens = tokenize(line);
        if (tokens.empty()) {
            return HCS_SUCCESS;
        }
        Instruction instruction;
        instruction.line = currentLine;

        std::string opToken;
        size_t offset = 0;
        if (tokens.size() >= 3 && tokens[1] == "=") {
            opToken = tokens[2];
            offset = 3;
        } else {
            opToken = tokens[0];
            offset = 1;
        }

        if (opToken == "invocation") {
            if (tokens.size() < 3 || tokens[1] != "=") {
                return error(HCS_E_INVALID_ARGUMENT, "invocation must define a value");
            }
            instruction.op = Op::Invocation;
            hcsResult defineResult = defineValue(tokens[0], Type::U32, &instruction.dst);
            if (defineResult != HCS_SUCCESS) {
                return defineResult;
            }
        } else if (opToken == "load") {
            if (offset + 2 != tokens.size()) {
                return error(HCS_E_INVALID_ARGUMENT, "expected 'load <buffer>, <index>'");
            }
            instruction.op = Op::Load;
            hcsResult defineResult = defineValue(tokens[0], Type::F32, &instruction.dst);
            if (defineResult != HCS_SUCCESS) {
                return defineResult;
            }
            auto buf = bufferOrdinals.find(tokens[offset]);
            if (buf == bufferOrdinals.end()) {
                return error(HCS_E_INVALID_ARGUMENT, "unknown buffer '" + tokens[offset] + "'");
            }
            instruction.buffer = static_cast<int>(buf->second);
            if (resolveOperand(tokens[offset + 1], &instruction.a, Type::U32) !=
                HCS_SUCCESS) {
                return HCS_E_INVALID_ARGUMENT;
            }
            Type indexType = Type::Void;
            operandType(instruction.a, &indexType);
            if (!isIntegerType(indexType)) {
                return error(HCS_E_INVALID_ARGUMENT, "load index must be integer");
            }
        } else if (opToken == "store") {
            if (offset + 3 != tokens.size()) {
                return error(HCS_E_INVALID_ARGUMENT,
                             "expected 'store <buffer>, <index>, <value>'");
            }
            instruction.op = Op::Store;
            auto buf = bufferOrdinals.find(tokens[offset]);
            if (buf == bufferOrdinals.end()) {
                return error(HCS_E_INVALID_ARGUMENT, "unknown buffer '" + tokens[offset] + "'");
            }
            instruction.buffer = static_cast<int>(buf->second);
            if (resolveOperand(tokens[offset + 1], &instruction.a, Type::U32) !=
                HCS_SUCCESS) {
                return HCS_E_INVALID_ARGUMENT;
            }
            Type indexType = Type::Void;
            operandType(instruction.a, &indexType);
            if (!isIntegerType(indexType)) {
                return error(HCS_E_INVALID_ARGUMENT, "store index must be integer");
            }
            if (resolveOperand(tokens[offset + 2], &instruction.b, Type::F32) !=
                HCS_SUCCESS) {
                return HCS_E_INVALID_ARGUMENT;
            }
            if (requireType(instruction.b, Type::F32, "store value") != HCS_SUCCESS) {
                return HCS_E_INVALID_ARGUMENT;
            }
        } else if (opToken == "add" || opToken == "sub" || opToken == "mul" ||
                   opToken == "min" || opToken == "max" || opToken == "fma") {
            const bool isFma = opToken == "fma";
            if (offset + (isFma ? 3 : 2) != tokens.size()) {
                return error(HCS_E_INVALID_ARGUMENT, "invalid arithmetic operand count");
            }
            if (isFma) {
                instruction.op = Op::Fma;
            } else if (opToken == "add") {
                instruction.op = Op::Add;
            } else if (opToken == "sub") {
                instruction.op = Op::Sub;
            } else if (opToken == "mul") {
                instruction.op = Op::Mul;
            } else if (opToken == "min") {
                instruction.op = Op::Min;
            } else {
                instruction.op = Op::Max;
            }
            hcsResult defineResult = defineValue(tokens[0], Type::F32, &instruction.dst);
            if (defineResult != HCS_SUCCESS) {
                return defineResult;
            }
            if (resolveOperand(tokens[offset], &instruction.a, Type::F32) != HCS_SUCCESS ||
                resolveOperand(tokens[offset + 1], &instruction.b, Type::F32) !=
                    HCS_SUCCESS) {
                return HCS_E_INVALID_ARGUMENT;
            }
            if (isFma && resolveOperand(tokens[offset + 2], &instruction.c, Type::F32) !=
                              HCS_SUCCESS) {
                return HCS_E_INVALID_ARGUMENT;
            }
            if (requireType(instruction.a, Type::F32, "arithmetic") != HCS_SUCCESS ||
                requireType(instruction.b, Type::F32, "arithmetic") != HCS_SUCCESS) {
                return HCS_E_INVALID_ARGUMENT;
            }
            if (isFma && requireType(instruction.c, Type::F32, "fma") != HCS_SUCCESS) {
                return HCS_E_INVALID_ARGUMENT;
            }
        } else if (opToken == "lt" || opToken == "le" || opToken == "gt" ||
                   opToken == "ge" || opToken == "eq" || opToken == "ne") {
            if (offset + 2 != tokens.size()) {
                return error(HCS_E_INVALID_ARGUMENT, "invalid comparison operand count");
            }
            if (opToken == "lt") {
                instruction.op = Op::CmpLt;
            } else if (opToken == "le") {
                instruction.op = Op::CmpLe;
            } else if (opToken == "gt") {
                instruction.op = Op::CmpGt;
            } else if (opToken == "ge") {
                instruction.op = Op::CmpGe;
            } else if (opToken == "eq") {
                instruction.op = Op::CmpEq;
            } else {
                instruction.op = Op::CmpNe;
            }
            hcsResult defineResult = defineValue(tokens[0], Type::Bool, &instruction.dst);
            if (defineResult != HCS_SUCCESS) {
                return defineResult;
            }
            if (resolveOperand(tokens[offset], &instruction.a, Type::F32) != HCS_SUCCESS ||
                resolveOperand(tokens[offset + 1], &instruction.b, Type::F32) !=
                    HCS_SUCCESS) {
                return HCS_E_INVALID_ARGUMENT;
            }
            Type left = Type::Void, right = Type::Void;
            operandType(instruction.a, &left);
            operandType(instruction.b, &right);
            if (!isNumeric(left) || left != right) {
                return error(HCS_E_INVALID_ARGUMENT,
                             "comparison operands must have the same numeric type");
            }
        } else if (opToken == "select") {
            if (offset + 3 != tokens.size()) {
                return error(HCS_E_INVALID_ARGUMENT,
                             "expected 'select <cond>, <a>, <b>'");
            }
            instruction.op = Op::Select;
            if (resolveOperand(tokens[offset], &instruction.a, Type::Bool) != HCS_SUCCESS ||
                resolveOperand(tokens[offset + 1], &instruction.b, Type::F32) !=
                    HCS_SUCCESS ||
                resolveOperand(tokens[offset + 2], &instruction.c, Type::F32) !=
                    HCS_SUCCESS) {
                return HCS_E_INVALID_ARGUMENT;
            }
            Type cond = Type::Void, left = Type::Void, right = Type::Void;
            operandType(instruction.a, &cond);
            operandType(instruction.b, &left);
            operandType(instruction.c, &right);
            if (cond != Type::Bool || left != right || !isNumeric(left)) {
                return error(HCS_E_INVALID_ARGUMENT, "invalid select operand types");
            }
            hcsResult defineResult = defineValue(tokens[0], left, &instruction.dst);
            if (defineResult != HCS_SUCCESS) {
                return defineResult;
            }
        } else if (opToken == "branch") {
            if (offset + 3 != tokens.size()) {
                return error(HCS_E_INVALID_ARGUMENT,
                             "expected 'branch <cond>, <trueLabel>, <falseLabel>'");
            }
            instruction.op = Op::Branch;
            if (resolveOperand(tokens[offset], &instruction.a, Type::Bool) != HCS_SUCCESS) {
                return HCS_E_INVALID_ARGUMENT;
            }
            if (requireType(instruction.a, Type::Bool, "branch condition") != HCS_SUCCESS) {
                return HCS_E_INVALID_ARGUMENT;
            }
            instruction.targetTrue = tokens[offset + 1];
            instruction.targetFalse = tokens[offset + 2];
            branchUses.push_back({currentLine, instruction.targetTrue});
            branchUses.push_back({currentLine, instruction.targetFalse});
        } else if (opToken == "jump") {
            if (offset + 1 != tokens.size()) {
                return error(HCS_E_INVALID_ARGUMENT, "expected 'jump <label>'");
            }
            instruction.op = Op::Jump;
            instruction.targetTrue = tokens[offset];
            branchUses.push_back({currentLine, instruction.targetTrue});
        } else if (opToken == "barrier") {
            if (offset != tokens.size()) {
                return error(HCS_E_INVALID_ARGUMENT, "barrier takes no operands");
            }
            instruction.op = Op::Barrier;
        } else if (opToken == "ret") {
            if (offset != tokens.size()) {
                return error(HCS_E_INVALID_ARGUMENT, "ret takes no operands");
            }
            instruction.op = Op::Ret;
        } else {
            return error(HCS_E_UNSUPPORTED_FEATURE, "unknown opcode '" + opToken + "'");
        }

        if (++module.instructionCount > kMaxInstructions) {
            return error(HCS_E_SECURITY_POLICY, "instruction limit exceeded");
        }
        block->code.push_back(std::move(instruction));
        return HCS_SUCCESS;
    }

    hcsResult parseBlockLabel(const std::string& line)
    {
        const std::string label = trim(line.substr(0, line.size() - 1));
        if (!isIdentifier(label)) {
            return error(HCS_E_INVALID_ARGUMENT, "invalid block label");
        }
        if (blockIndex.count(label) != 0) {
            return error(HCS_E_INVALID_ARGUMENT, "duplicate block label '" + label + "'");
        }
        if (module.blocks.size() >= kMaxBlocks) {
            return error(HCS_E_SECURITY_POLICY, "block limit exceeded");
        }
        Block block;
        block.name = label;
        block.line = currentLine;
        blockIndex[label] = static_cast<uint32_t>(module.blocks.size());
        module.blocks.push_back(std::move(block));
        return HCS_SUCCESS;
    }

    hcsResult verifyStructure()
    {
        if (module.kernel.empty()) {
            return error(HCS_E_INVALID_ARGUMENT, "missing kernel declaration");
        }
        if (module.localSize == 0) {
            return error(HCS_E_INVALID_ARGUMENT, "missing local_size declaration");
        }
        if (module.blocks.empty()) {
            return error(HCS_E_INVALID_ARGUMENT, "module has no blocks");
        }
        for (const auto& entry : branchUses) {
            if (blockIndex.count(entry.second) == 0) {
                currentLine = entry.first;
                return error(HCS_E_INVALID_ARGUMENT,
                             "branch target '" + entry.second + "' does not exist");
            }
        }
        for (auto& block : module.blocks) {
            if (block.code.empty()) {
                currentLine = block.line;
                return error(HCS_E_INVALID_ARGUMENT, "empty block '" + block.name + "'");
            }
            const Op last = block.code.back().op;
            if (last != Op::Ret && last != Op::Jump && last != Op::Branch) {
                currentLine = block.code.back().line;
                return error(HCS_E_INVALID_ARGUMENT,
                             "block '" + block.name + "' has no terminator");
            }
            for (size_t i = 0; i + 1 < block.code.size(); ++i) {
                const Op op = block.code[i].op;
                if (op == Op::Ret || op == Op::Jump || op == Op::Branch) {
                    currentLine = block.code[i].line;
                    return error(HCS_E_INVALID_ARGUMENT,
                                 "instruction after terminator in block '" + block.name + "'");
                }
            }
        }
        /* Values defined in a non-entry block are only usable inside it. */
        for (const auto& block : module.blocks) {
            if (block.name == module.blocks.front().name) {
                continue;
            }
            for (const auto& instruction : block.code) {
                if (instruction.dst < 0) {
                    continue;
                }
                for (const auto& other : module.blocks) {
                    if (other.name == block.name) {
                        continue;
                    }
                    for (const auto& candidate : other.code) {
                        const Operand operands[3] = {candidate.a, candidate.b, candidate.c};
                        for (const auto& operand : operands) {
                            if (operand.kind == Operand::Kind::Value &&
                                operand.id == static_cast<uint32_t>(instruction.dst)) {
                                currentLine = candidate.line;
                                return error(HCS_E_INVALID_ARGUMENT,
                                             "value defined in block '" + block.name +
                                                 "' used outside that block");
                            }
                        }
                    }
                }
            }
        }
        return HCS_SUCCESS;
    }

    hcsResult parse()
    {
        if (source.size() > kMaxSourceBytes) {
            currentLine = 0;
            return error(HCS_E_SECURITY_POLICY, "source exceeds 1 MiB limit");
        }
        /* Split into lines without allocating a copy of the whole source. */
        size_t start = 0;
        uint32_t lineNumber = 0;
        while (start <= source.size()) {
            size_t end = source.find('\n', start);
            if (end == std::string::npos) {
                end = source.size();
            }
            std::string line = source.substr(start, end - start);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            ++lineNumber;
            currentLine = lineNumber;
            const std::string trimmed = trim(line);
            if (!trimmed.empty() && trimmed[0] != '#') {
                lines.push_back(trimmed);
                lineNumbers.push_back(lineNumber);
            }
            if (end == source.size()) {
                break;
            }
            start = end + 1;
        }
        if (lines.empty()) {
            currentLine = 0;
            return error(HCS_E_INVALID_ARGUMENT, "empty module");
        }

        currentLine = lineNumbers[0];
        hcsResult result = parseHeader(lines[0]);
        if (result != HCS_SUCCESS) {
            return result;
        }

        bool sawBlock = false;
        for (size_t i = 1; i < lines.size(); ++i) {
            const std::string& line = lines[i];
            currentLine = lineNumbers[i];
            if (!sawBlock && !line.empty() && line.back() == ':') {
                result = parseBlockLabel(line);
                if (result != HCS_SUCCESS) {
                    return result;
                }
                sawBlock = true;
                continue;
            }
            const std::vector<std::string> tokens = tokenize(line);
            if (tokens.empty()) {
                continue;
            }
            if (!sawBlock) {
                if (tokens[0] == "kernel") {
                    if (tokens.size() != 2 || !isIdentifier(tokens[1])) {
                        return error(HCS_E_INVALID_ARGUMENT, "expected 'kernel <name>'");
                    }
                    if (!module.kernel.empty()) {
                        return error(HCS_E_INVALID_ARGUMENT, "duplicate kernel declaration");
                    }
                    module.kernel = tokens[1];
                } else if (tokens[0] == "buffer") {
                    result = parseBuffer(tokens);
                } else if (tokens[0] == "param") {
                    result = parseParam(tokens);
                } else if (tokens[0] == "const") {
                    result = parseConstant(tokens);
                } else if (tokens[0] == "local_size") {
                    uint64_t size = 0;
                    if (tokens.size() != 2 || !parseUnsigned(tokens[1], &size) || size == 0 ||
                        size > 1024) {
                        return error(HCS_E_INVALID_ARGUMENT,
                                     "local_size must be 1..1024");
                    }
                    module.localSize = static_cast<uint32_t>(size);
                } else {
                    return error(HCS_E_INVALID_ARGUMENT,
                                 "unexpected declaration '" + tokens[0] + "'");
                }
                if (result != HCS_SUCCESS) {
                    return result;
                }
            } else {
                if (!line.empty() && line.back() == ':') {
                    result = parseBlockLabel(line);
                    if (result != HCS_SUCCESS) {
                        return result;
                    }
                    continue;
                }
                Block& block = module.blocks.back();
                result = parseInstruction(line, &block);
                if (result != HCS_SUCCESS) {
                    return result;
                }
            }
        }

        if (module.localSize == 0) {
            /* Default to a documented value only when the module omits it? No:
             * require it explicitly so profiles cannot silently differ. */
            return error(HCS_E_INVALID_ARGUMENT, "missing local_size declaration");
        }
        return verifyStructure();
    }
};

bool operandValue(const Operand& operand, const std::vector<double>& values, double* out)
{
    if (operand.kind == Operand::Kind::Value) {
        *out = values[operand.id];
        return true;
    }
    if (operand.kind == Operand::Kind::FloatLit || operand.kind == Operand::Kind::IntLit) {
        *out = operand.number;
        return true;
    }
    return false;
}

} // namespace

const char* typeName(Type type)
{
    switch (type) {
    case Type::F32: return "f32";
    case Type::U32: return "u32";
    case Type::I32: return "i32";
    case Type::Bool: return "bool";
    default: return "void";
    }
}

const char* opName(Op op)
{
    switch (op) {
    case Op::Invocation: return "invocation";
    case Op::Load: return "load";
    case Op::Store: return "store";
    case Op::Add: return "add";
    case Op::Sub: return "sub";
    case Op::Mul: return "mul";
    case Op::Fma: return "fma";
    case Op::Min: return "min";
    case Op::Max: return "max";
    case Op::CmpLt: return "lt";
    case Op::CmpLe: return "le";
    case Op::CmpGt: return "gt";
    case Op::CmpGe: return "ge";
    case Op::CmpEq: return "eq";
    case Op::CmpNe: return "ne";
    case Op::Select: return "select";
    case Op::Branch: return "branch";
    case Op::Jump: return "jump";
    case Op::Barrier: return "barrier";
    case Op::Ret: return "ret";
    default: return "?";
    }
}

hcsResult compile(const char* source, size_t size, Module* out)
{
    if (!source || !out) {
        return fail(HCS_E_INVALID_ARGUMENT, "hcsir.compile",
                    "source and output module must not be null");
    }
    Parser parser;
    parser.source.assign(source, size);
    *out = Module{};
    const hcsResult result = parser.parse();
    if (result != HCS_SUCCESS) {
        return result;
    }
    *out = std::move(parser.module);
    return HCS_SUCCESS;
}

hcsResult run(const Module& module, const RunArgs& args, uint32_t invocationCount)
{
    if (module.blocks.empty() || module.valueTypes.empty()) {
        return fail(HCS_E_INVALID_ARGUMENT, "hcsir.run", "module is not compiled");
    }
    if (args.bufferCount > kMaxBuffers) {
        return fail(HCS_E_INVALID_ARGUMENT, "hcsir.run", "too many buffers");
    }
    for (const auto& decl : module.buffers) {
        if (decl.index >= args.bufferCount || !args.buffers[decl.index]) {
            return fail(HCS_E_INVALID_ARGUMENT, "hcsir.run",
                        "buffer '" + decl.name + "' is not bound");
        }
        if (args.sizes[decl.index] % sizeof(float) != 0) {
            return fail(HCS_E_INVALID_ARGUMENT, "hcsir.run",
                        "buffer '" + decl.name + "' size is not a multiple of 4 bytes");
        }
    }
    if (invocationCount > (1u << 22)) {
        return fail(HCS_E_SECURITY_POLICY, "hcsir.run",
                    "invocation count exceeds the 4M limit");
    }

    std::vector<double> values(module.valueTypes.size(), 0.0);
    std::vector<bool> isSet(module.valueTypes.size(), false);
    for (const ConstantDecl& constant : module.constants) {
        if (constant.valueId < values.size()) {
            values[constant.valueId] = constant.value;
            isSet[constant.valueId] = true;
        }
    }
    for (uint32_t p = 0; p < module.params.size(); ++p) {
        const ParamDecl& param = module.params[p];
        const double value = p < args.paramCount ? args.params[p] : 0.0;
        values[param.valueId] = value;
        isSet[param.valueId] = true;
    }

    std::unordered_map<std::string, uint32_t> blockIndex;
    for (uint32_t i = 0; i < module.blocks.size(); ++i) {
        blockIndex[module.blocks[i].name] = i;
    }

    std::vector<float*> bufferPointers(module.buffers.size(), nullptr);
    std::vector<uint64_t> bufferCounts(module.buffers.size(), 0);
    for (size_t b = 0; b < module.buffers.size(); ++b) {
        const BufferDecl& decl = module.buffers[b];
        bufferPointers[b] = static_cast<float*>(args.buffers[decl.index]);
        bufferCounts[b] = args.sizes[decl.index] / sizeof(float);
    }

    for (uint32_t invocation = 0; invocation < invocationCount; ++invocation) {
        uint32_t block = 0;
        uint64_t steps = 0;
        bool finished = false;
        while (!finished) {
            if (block >= module.blocks.size()) {
                return fail(HCS_E_INTERNAL, "hcsir.run", "control flow left the module");
            }
            const Block& current = module.blocks[block];
            int nextBlock = -1;
            bool terminated = false;
            for (const Instruction& instruction : current.code) {
                if (++steps > kMaxStepsPerInvocation) {
                    return fail(HCS_E_SECURITY_POLICY, "hcsir.run",
                                "instruction step limit exceeded (possible infinite loop)");
                }
                switch (instruction.op) {
                case Op::Invocation:
                    values[instruction.dst] = static_cast<double>(invocation);
                    isSet[instruction.dst] = true;
                    break;
                case Op::Load: {
                    double indexValue = 0.0;
                    operandValue(instruction.a, values, &indexValue);
                    if (indexValue < 0 || indexValue >= static_cast<double>(bufferCounts[instruction.buffer])) {
                        return fail(HCS_E_INVALID_ARGUMENT, "hcsir.run",
                                    "load out of bounds in buffer '" +
                                        module.buffers[instruction.buffer].name + "'");
                    }
                    values[instruction.dst] =
                        static_cast<double>(bufferPointers[instruction.buffer]
                                                          [static_cast<uint64_t>(indexValue)]);
                    isSet[instruction.dst] = true;
                    break;
                }
                case Op::Store: {

                    double indexValue = 0.0;
                    double storeValue = 0.0;
                    operandValue(instruction.a, values, &indexValue);
                    operandValue(instruction.b, values, &storeValue);
                    if (indexValue < 0 || indexValue >= static_cast<double>(bufferCounts[instruction.buffer])) {
                        return fail(HCS_E_INVALID_ARGUMENT, "hcsir.run",
                                    "store out of bounds in buffer '" +
                                        module.buffers[instruction.buffer].name + "'");
                    }
                    bufferPointers[instruction.buffer][static_cast<uint64_t>(indexValue)] =
                        static_cast<float>(storeValue);
                    break;
                }
                case Op::Add:
                case Op::Sub:
                case Op::Mul:
                case Op::Fma:
                case Op::Min:
                case Op::Max: {
                    double a = 0.0, b = 0.0, c = 0.0;
                    operandValue(instruction.a, values, &a);
                    operandValue(instruction.b, values, &b);
                    if (instruction.op == Op::Fma) {
                        operandValue(instruction.c, values, &c);
                    }
                    double result = 0.0;
                    switch (instruction.op) {
                    case Op::Add: result = a + b; break;
                    case Op::Sub: result = a - b; break;
                    case Op::Mul: result = a * b; break;
                    case Op::Fma: result = a * b + c; break;
                    case Op::Min: result = std::fmin(a, b); break;
                    case Op::Max: result = std::fmax(a, b); break;
                    default: break;
                    }
                    values[instruction.dst] = result;
                    isSet[instruction.dst] = true;
                    break;
                }
                case Op::CmpLt:
                case Op::CmpLe:
                case Op::CmpGt:
                case Op::CmpGe:
                case Op::CmpEq:
                case Op::CmpNe: {
                    double a = 0.0, b = 0.0;
                    operandValue(instruction.a, values, &a);
                    operandValue(instruction.b, values, &b);
                    bool result = false;
                    switch (instruction.op) {
                    case Op::CmpLt: result = a < b; break;
                    case Op::CmpLe: result = a <= b; break;
                    case Op::CmpGt: result = a > b; break;
                    case Op::CmpGe: result = a >= b; break;
                    case Op::CmpEq: result = a == b; break;
                    case Op::CmpNe: result = a != b; break;
                    default: break;
                    }
                    values[instruction.dst] = result ? 1.0 : 0.0;
                    isSet[instruction.dst] = true;
                    break;
                }
                case Op::Select: {
                    double cond = 0.0, a = 0.0, b = 0.0;
                    operandValue(instruction.a, values, &cond);
                    operandValue(instruction.b, values, &a);
                    operandValue(instruction.c, values, &b);
                    values[instruction.dst] = cond != 0.0 ? a : b;
                    isSet[instruction.dst] = true;
                    break;
                }
                case Op::Branch: {
                    double cond = 0.0;

                    operandValue(instruction.a, values, &cond);
                    const std::string& target =
                        cond != 0.0 ? instruction.targetTrue : instruction.targetFalse;
                    nextBlock = static_cast<int>(blockIndex[target]);
                    terminated = true;
                    break;
                }
                case Op::Jump:
                    nextBlock = static_cast<int>(blockIndex[instruction.targetTrue]);
                    terminated = true;
                    break;
                case Op::Barrier:
                    break; /* CPU interpreter is single-threaded per invocation */
                case Op::Ret:
                    terminated = true;
                    finished = true;
                    break;
                default:
                    return fail(HCS_E_UNSUPPORTED_FEATURE, "hcsir.run",
                                "unhandled opcode in interpreter");
                }
                if (terminated) {
                    break;
                }
            }
            if (!terminated) {
                return fail(HCS_E_INTERNAL, "hcsir.run",
                            "block fell through without a terminator");
            }
            if (nextBlock >= 0) {
                block = static_cast<uint32_t>(nextBlock);
            }
        }
    }
    (void)isSet;
    return HCS_SUCCESS;
}

} // namespace hcs::ir
