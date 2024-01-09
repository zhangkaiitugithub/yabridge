// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2024 Robbert van der Helm
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <pluginterfaces/vst/ivstprefetchablesupport.h>

#include "../../common.h"
#include "../base.h"
#include "../bstream.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * Wraps around `IPrefetchableSupport` for serialization purposes. This is
 * instantiated as part of `Vst3PluginProxy`.
 */
class YaPrefetchableSupport : public Steinberg::Vst::IPrefetchableSupport {
   public:
    /**
     * These are the arguments for creating a `YaPrefetchableSupport`.
     */
    struct ConstructArgs {
        ConstructArgs() noexcept;

        /**
         * Check whether an existing implementation implements
         * `IPrefetchableSupport` and read arguments from it.
         */
        ConstructArgs(Steinberg::IPtr<Steinberg::FUnknown> object) noexcept;

        /**
         * Whether the object supported this interface.
         */
        bool supported;

        template <typename S>
        void serialize(S& s) {
            s.value1b(supported);
        }
    };

    /**
     * Instantiate this instance with arguments read from another interface
     * implementation.
     */
    YaPrefetchableSupport(ConstructArgs&& args) noexcept;

    virtual ~YaPrefetchableSupport() noexcept = default;

    inline bool supported() const noexcept { return arguments_.supported; }

    /**
     * The response code and returned bus information for a call to
     * `IPrefetchableSupport::getPrefetchableSupport(&prefetchable)`.
     */
    struct GetPrefetchableSupportResponse {
        UniversalTResult result;
        Steinberg::Vst::PrefetchableSupport prefetchable;

        template <typename S>
        void serialize(S& s) {
            s.object(result);
            s.value4b(prefetchable);
        }
    };

    /**
     * Message to pass through a call to
     * `IPrefetchableSupport::getPrefetchableSupport(&prefetchable)` to the Wine
     * plugin host.
     */
    struct GetPrefetchableSupport {
        using Response = GetPrefetchableSupportResponse;

        native_size_t instance_id;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
        }
    };

    virtual tresult PLUGIN_API getPrefetchableSupport(
        Steinberg::Vst::PrefetchableSupport& prefetchable /*out*/) override = 0;

   protected:
    ConstructArgs arguments_;
};

#pragma GCC diagnostic pop
