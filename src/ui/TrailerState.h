#pragma once

namespace ns {
enum class TrailerState { Buffering, DownloadingFile, Rebuffering, Playing, Finished, LinkGone, ConnectionFailed, Unsupported, PlayerFailed };

inline bool offerTrailerLink(TrailerState state)
{
    return state == TrailerState::ConnectionFailed || state == TrailerState::Unsupported || state == TrailerState::PlayerFailed;
}
} // namespace ns
