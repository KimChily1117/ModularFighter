// 메시지 직렬화와 프레임 단위 수신.
#include "modular_fighter/match_protocol.hpp"

#include <type_traits>

namespace modular_fighter::net {
namespace {

// 메시지를 바이트로 쓴다.
class Writer {
public:
    void U8(std::uint8_t v) {
        bytes.push_back(v);
    }

    void U16(std::uint16_t v) {
        for (int i = 0; i < 2; ++i)
            bytes.push_back(static_cast<std::uint8_t>(v >> (i * 8)));
    }

    void U32(std::uint32_t v) {
        for (int i = 0; i < 4; ++i)
            bytes.push_back(static_cast<std::uint8_t>(v >> (i * 8)));
    }

    void U64(std::uint64_t v) {
        for (int i = 0; i < 8; ++i)
            bytes.push_back(static_cast<std::uint8_t>(v >> (i * 8)));
    }

    void I8(std::int8_t v) {
        U8(static_cast<std::uint8_t>(v));
    }

    std::vector<std::uint8_t> bytes;
};

// 범위를 확인하며 바이트를 읽는다.
class Reader {
public:
    Reader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}

    bool U8(std::uint8_t& v) {
        if (cursor_ + 1 > size_)
            return false;
        v = data_[cursor_++];

        return true;
    }

    bool U16(std::uint16_t& v) {
        std::uint64_t x;

        if (!Le(2, x))
            return false;
        v = static_cast<std::uint16_t>(x);

        return true;
    }

    bool U32(std::uint32_t& v) {
        std::uint64_t x;

        if (!Le(4, x))
            return false;
        v = static_cast<std::uint32_t>(x);

        return true;
    }

    bool U64(std::uint64_t& v) {
        return Le(8, v);
    }

    bool I8(std::int8_t& v) {
        std::uint8_t x;

        if (!U8(x))
            return false;
        v = static_cast<std::int8_t>(x <= 127 ? static_cast<int>(x) : static_cast<int>(x) - 256);

        return true;
    }

    bool Done() const {
        return cursor_ == size_;
    }

private:
    bool Le(std::size_t n, std::uint64_t& v) {
        if (cursor_ + n > size_)
            return false;
        v = 0;

        for (std::size_t i = 0; i < n; ++i)
            v |= static_cast<std::uint64_t>(data_[cursor_++]) << (i * 8);

        return true;
    }

    const std::uint8_t* data_;
    std::size_t size_, cursor_ = 0;
};

bool ValidSide(std::uint8_t v, bool allow_none) {
    return v <= 1 || (allow_none && v == 0xFF);
}

bool ValidCharacter(std::uint8_t v) {
    return v <= static_cast<std::uint8_t>(CharacterId::Paul);
}

void Put(Writer& w, const Input& in) {
    w.I8(in.horizontal);
    w.U8(in.buttons);
    w.I8(in.vertical);
    w.U8(in.training_rage);
    w.U8(in.view_flipped);
}

bool Get(Reader& r, Input& in) {
    return r.I8(in.horizontal) && r.U8(in.buttons) && r.I8(in.vertical) && r.U8(in.training_rage) &&
           r.U8(in.view_flipped) && IsValid(in);
}

void Body(Writer& w, const Hello& m) {
    w.U16(m.protocol);
    w.U64(m.rules_fingerprint);
}

void Body(Writer&, const QueueMatch&) {}

void Body(Writer& w, const SelectSide& m) {
    w.U8(static_cast<std::uint8_t>(m.side));
}

void Body(Writer& w, const SelectCharacter& m) {
    w.U8(static_cast<std::uint8_t>(m.character));
}

void Body(Writer&, const Ready&) {}

void Body(Writer& w, const ResultReport& m) {
    w.U32(m.frame);
    w.U8(m.winner);
    w.U64(m.state_hash);
}

void Body(Writer& w, const Rematch& m) {
    w.U8(m.accept ? 1 : 0);
}

void Body(Writer&, const Leave&) {}

void Body(Writer& w, const SelectStage& m) {
    w.U8(m.stage);
}

void Body(Writer& w, const StageLoaded& m) {
    w.U32(m.match_id);
}

void Body(Writer& w, const LoadStage& m) {
    w.U32(m.match_id);
    w.U8(m.stage);
    w.U8(static_cast<std::uint8_t>(m.left));
    w.U8(static_cast<std::uint8_t>(m.right));
}

void Body(Writer& w, const InputBatch& m) {
    w.U32(m.first_frame);
    w.U32(m.ack_frame);
    w.U8(static_cast<std::uint8_t>(m.inputs.size()));

    for (const auto& in : m.inputs)
        Put(w, in);
}

void Body(Writer& w, const Welcome& m) {
    w.U32(m.client_id);
}

void Body(Writer& w, const RoomState& m) {
    w.U32(m.room_id);
    w.U8(m.your_slot);
    w.U8(static_cast<std::uint8_t>(m.phase));

    for (const auto& s : m.slots) {
        w.U8(s.connected);
        w.U8(static_cast<std::uint8_t>(s.side));
        w.U8(static_cast<std::uint8_t>(s.character));
        w.U8(s.ready);
        w.U8(s.rematch);
        w.U8(s.stage);
    }
}

void Body(Writer& w, const MatchStart& m) {
    w.U32(m.match_id);
    w.U8(m.your_slot);
    w.U8(static_cast<std::uint8_t>(m.your_side));
    w.U8(static_cast<std::uint8_t>(m.left));
    w.U8(static_cast<std::uint8_t>(m.right));
    w.U8(m.input_delay);
    w.U8(m.round);
    w.U8(m.left_wins);
    w.U8(m.right_wins);
    w.U8(m.rounds_to_win);
    w.U8(m.stage);
}

void Body(Writer& w, const MatchResult& m) {
    w.U32(m.match_id);
    w.U8(static_cast<std::uint8_t>(m.status));
    w.U8(m.winner);
}

void Body(Writer& w, const Error& m) {
    w.U8(static_cast<std::uint8_t>(m.code));
}

bool Bool(Reader& r, bool& v) {
    std::uint8_t x;

    if (!r.U8(x) || x > 1)
        return false;
    v = x != 0;

    return true;
}

bool Body(Reader& r, Hello& m) {
    return r.U16(m.protocol) && r.U64(m.rules_fingerprint);
}

bool Body(Reader&, QueueMatch&) {
    return true;
}

bool Body(Reader& r, SelectSide& m) {
    std::uint8_t v;

    if (!r.U8(v) || !ValidSide(v, false))
        return false;
    m.side = static_cast<Side>(v);

    return true;
}

bool Body(Reader& r, SelectCharacter& m) {
    std::uint8_t v;

    if (!r.U8(v) || !ValidCharacter(v))
        return false;
    m.character = static_cast<CharacterId>(v);

    return true;
}

bool Body(Reader&, Ready&) {
    return true;
}

bool Body(Reader& r, ResultReport& m) {
    return r.U32(m.frame) && r.U8(m.winner) && m.winner <= 2 && r.U64(m.state_hash);
}

bool Body(Reader& r, Rematch& m) {
    return Bool(r, m.accept);
}

bool Body(Reader&, Leave&) {
    return true;
}

bool Body(Reader& r, SelectStage& m) {
    return r.U8(m.stage) && m.stage < StageCount;
}

bool Body(Reader& r, StageLoaded& m) {
    return r.U32(m.match_id);
}

bool Body(Reader& r, LoadStage& m) {
    std::uint8_t left, right;

    if (!r.U32(m.match_id) || !r.U8(m.stage) || m.stage >= StageCount || !r.U8(left) ||
        !ValidCharacter(left) || !r.U8(right) || !ValidCharacter(right))
        return false;
    m.left = static_cast<CharacterId>(left);
    m.right = static_cast<CharacterId>(right);

    return true;
}

bool Body(Reader& r, InputBatch& m) {
    std::uint8_t count;

    if (!r.U32(m.first_frame) || !r.U32(m.ack_frame) || !r.U8(count) || count > MaxBatchInputs)
        return false;
    m.inputs.resize(count);

    for (auto& in : m.inputs)
        if (!Get(r, in))
            return false;

    return true;
}

bool Body(Reader& r, Welcome& m) {
    return r.U32(m.client_id);
}

bool Body(Reader& r, RoomState& m) {
    std::uint8_t phase;

    if (!r.U32(m.room_id) || !r.U8(m.your_slot) || m.your_slot > 1 || !r.U8(phase) ||
        phase > static_cast<std::uint8_t>(Phase::Loading))
        return false;
    m.phase = static_cast<Phase>(phase);

    for (auto& s : m.slots) {
        std::uint8_t side, character;

        if (!Bool(r, s.connected) || !r.U8(side) || !ValidSide(side, true) || !r.U8(character) ||
            !ValidCharacter(character) || !Bool(r, s.ready) || !Bool(r, s.rematch) || !r.U8(s.stage) ||
            s.stage >= StageCount)
            return false;
        s.side = static_cast<Side>(side);
        s.character = static_cast<CharacterId>(character);
    }

    return true;
}

bool Body(Reader& r, MatchStart& m) {
    std::uint8_t side, left, right;

    if (!r.U32(m.match_id) || !r.U8(m.your_slot) || m.your_slot > 1 || !r.U8(side) ||
        !ValidSide(side, false) || !r.U8(left) || !ValidCharacter(left) || !r.U8(right) ||
        !ValidCharacter(right) || !r.U8(m.input_delay) || !r.U8(m.round) || m.round < 1 ||
        !r.U8(m.left_wins) || !r.U8(m.right_wins) || !r.U8(m.rounds_to_win) || m.rounds_to_win < 1 ||
        !r.U8(m.stage) || m.stage >= StageCount)
        return false;
    m.your_side = static_cast<Side>(side);
    m.left = static_cast<CharacterId>(left);
    m.right = static_cast<CharacterId>(right);

    return true;
}

bool Body(Reader& r, MatchResult& m) {
    std::uint8_t status;

    if (!r.U32(m.match_id) || !r.U8(status) || status > 2 || !r.U8(m.winner) || m.winner > 2)
        return false;
    m.status = static_cast<ResultStatus>(status);

    return true;
}

bool Body(Reader& r, Error& m) {
    std::uint8_t code;

    if (!r.U8(code) || code < 1 || code > static_cast<std::uint8_t>(ErrorCode::NotInRoom))
        return false;
    m.code = static_cast<ErrorCode>(code);

    return true;
}

template <std::size_t I = 0> bool DecodeAlternative(std::size_t index, Reader& r, Message& out) {
    if constexpr (I < std::variant_size_v<Message>) {
        if (index == I) {
            std::variant_alternative_t<I, Message> value{};

            if (!Body(r, value))
                return false;
            out = std::move(value);

            return true;
        }

        return DecodeAlternative<I + 1>(index, r, out);
    } else {
        return false;
    }
}

}

std::vector<std::uint8_t> Encode(const Message& message) {
    Writer body;
    std::visit([&body](const auto& m) { Body(body, m); }, message);
    Writer frame;
    frame.U16(static_cast<std::uint16_t>(body.bytes.size()));
    frame.U8(static_cast<std::uint8_t>(message.index() + 1));
    frame.bytes.insert(frame.bytes.end(), body.bytes.begin(), body.bytes.end());

    return frame.bytes;
}

std::optional<Message> Decode(const std::uint8_t* data, std::size_t size) {
    if (size < 3)
        return std::nullopt;
    const std::size_t length = data[0] | (static_cast<std::size_t>(data[1]) << 8);

    if (length > MaxFramePayload || size != length + 3 || data[2] == 0)
        return std::nullopt;
    Reader r(data + 3, length);
    Message out;

    if (!DecodeAlternative(data[2] - 1u, r, out) || !r.Done())
        return std::nullopt;

    return out;
}

void FrameReader::Append(const std::uint8_t* data, std::size_t size) {
    buffer_.insert(buffer_.end(), data, data + size);
}

// 수신 버퍼에서 완성된 메시지를 꺼낸다.
std::optional<Message> FrameReader::Next() {
    if (failed_ || buffer_.size() < 3)
        return std::nullopt;
    const std::size_t length = buffer_[0] | (static_cast<std::size_t>(buffer_[1]) << 8);

    if (length > MaxFramePayload) {
        failed_ = true;

        return std::nullopt;
    }

    if (buffer_.size() < length + 3)
        return std::nullopt;
    auto message = Decode(buffer_.data(), length + 3);
    buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(length + 3));

    if (!message)
        failed_ = true;

    return message;
}

}
