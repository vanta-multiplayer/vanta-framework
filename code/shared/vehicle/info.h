#pragma once

#include <cstdint>
#include <string_view>

namespace VantaMP::Shared {
    // MTA vehicleType strings, from the retail model id — not a GTA enum
    // the server can ask the exe for.
    inline constexpr const char *VehicleTypeName(std::uint32_t modelId) {
        switch (modelId) {
        case 481:
        case 509:
        case 510:
            return "BMX";
        case 448:
        case 461:
        case 462:
        case 463:
        case 468:
        case 521:
        case 522:
        case 523:
        case 581:
        case 586:
            return "Bike";
        case 471:
            return "Quad";
        case 430:
        case 446:
        case 452:
        case 453:
        case 454:
        case 472:
        case 473:
        case 484:
        case 493:
        case 595:
            return "Boat";
        case 417:
        case 425:
        case 447:
        case 469:
        case 487:
        case 488:
        case 497:
        case 548:
        case 563:
            return "Helicopter";
        case 460:
        case 476:
        case 511:
        case 512:
        case 513:
        case 519:
        case 520:
        case 553:
        case 577:
        case 592:
        case 593:
            return "Plane";
        case 449:
        case 537:
        case 538:
        case 569:
        case 570:
        case 590:
            return "Train";
        case 435:
        case 450:
        case 584:
        case 591:
        case 606:
        case 607:
        case 608:
        case 610:
        case 611:
            return "Trailer";
        default:
            return "Automobile";
        }
    }

    inline constexpr std::uint8_t VehicleMaxPassengers(std::uint32_t modelId) {
        switch (modelId) {
        case 431:
        case 437:
            return 8;
        case 408:
        case 416:
        case 423:
        case 427:
        case 428:
        case 433:
        case 508:
        case 544:
        case 573:
        case 578:
            return 1;
        default:
            break;
        }

        const auto *type = VehicleTypeName(modelId);

        if (type[0] == 'B' && (type[1] == 'i' || type[1] == 'M')) {
            return 1;
        }

        if (type[0] == 'Q' || type[0] == 'T') {
            return type[1] == 'r' ? 0 : 1;
        }

        return 3;
    }

    inline constexpr std::string_view VehicleName(std::uint32_t modelId) {
        switch (modelId) {
        case 400: return "Landstalker";
        case 401: return "Bravura";
        case 402: return "Buffalo";
        case 403: return "Linerunner";
        case 404: return "Perennial";
        case 405: return "Sentinel";
        case 406: return "Dumper";
        case 407: return "Firetruck";
        case 408: return "Trashmaster";
        case 409: return "Stretch";
        case 410: return "Manana";
        case 411: return "Infernus";
        case 412: return "Voodoo";
        case 413: return "Pony";
        case 414: return "Mule";
        case 415: return "Cheetah";
        case 416: return "Ambulance";
        case 417: return "Leviathan";
        case 418: return "Moonbeam";
        case 419: return "Esperanto";
        case 420: return "Taxi";
        case 421: return "Washington";
        case 422: return "Bobcat";
        case 423: return "Mr Whoopee";
        case 424: return "BF Injection";
        case 425: return "Hunter";
        case 426: return "Premier";
        case 427: return "Enforcer";
        case 428: return "Securicar";
        case 429: return "Banshee";
        case 430: return "Predator";
        case 431: return "Bus";
        case 432: return "Rhino";
        case 433: return "Barracks";
        case 434: return "Hotknife";
        case 436: return "Previon";
        case 437: return "Coach";
        case 438: return "Cabbie";
        case 439: return "Stallion";
        case 440: return "Rumpo";
        case 442: return "Romero";
        case 443: return "Packer";
        case 444: return "Monster";
        case 445: return "Admiral";
        case 446: return "Squalo";
        case 447: return "Seasparrow";
        case 448: return "Pizzaboy";
        case 451: return "Turismo";
        case 452: return "Speeder";
        case 453: return "Reefer";
        case 454: return "Tropic";
        case 456: return "Yankee";
        case 457: return "Caddy";
        case 458: return "Solair";
        case 459: return "Berkley's RC Van";
        case 461: return "PCJ-600";
        case 462: return "Faggio";
        case 463: return "Freeway";
        case 466: return "Glendale";
        case 467: return "Oceanic";
        case 468: return "Sanchez";
        case 470: return "Patriot";
        case 471: return "Quad";
        case 474: return "Hermes";
        case 475: return "Sabre";
        case 477: return "ZR-350";
        case 478: return "Walton";
        case 479: return "Regina";
        case 480: return "Comet";
        case 481: return "BMX";
        case 482: return "Burrito";
        case 483: return "Camper";
        case 485: return "Baggage";
        case 486: return "Dozer";
        case 487: return "Maverick";
        case 489: return "Rancher";
        case 490: return "FBI Rancher";
        case 491: return "Virgo";
        case 492: return "Greenwood";
        case 494: return "Hotring Racer";
        case 495: return "Sandking";
        case 496: return "Blista Compact";
        case 498: return "Boxville";
        case 499: return "Benson";
        case 500: return "Mesa";
        case 502: return "Hotring Racer A";
        case 503: return "Hotring Racer B";
        case 504: return "Bloodring Banger";
        case 505: return "Rancher Lure";
        case 506: return "Super GT";
        case 507: return "Elegant";
        case 508: return "Journey";
        case 516: return "Nebula";
        case 517: return "Majestic";
        case 518: return "Buccaneer";
        case 525: return "Towtruck";
        case 526: return "Fortune";
        case 527: return "Cadrona";
        case 528: return "FBI Truck";
        case 529: return "Willard";
        case 533: return "Feltzer";
        case 534: return "Remington";
        case 535: return "Slamvan";
        case 536: return "Blade";
        case 540: return "Vincent";
        case 541: return "Bullet";
        case 542: return "Clover";
        case 543: return "Sadler";
        case 545: return "Hustler";
        case 546: return "Intruder";
        case 547: return "Primo";
        case 549: return "Tampa";
        case 550: return "Sunrise";
        case 551: return "Merit";
        case 554: return "Yosemite";
        case 555: return "Windsor";
        case 558: return "Uranus";
        case 559: return "Jester";
        case 560: return "Sultan";
        case 561: return "Stratum";
        case 562: return "Elegy";
        case 565: return "Flash";
        case 566: return "Tahoma";
        case 567: return "Savanna";
        case 575: return "Broadway";
        case 576: return "Tornado";
        case 579: return "Huntley";
        case 580: return "Stafford";
        case 582: return "News Van";
        case 585: return "Emperor";
        case 587: return "Euros";
        case 588: return "Hotdog";
        case 589: return "Club";
        case 596: return "Police LS";
        case 597: return "Police SF";
        case 598: return "Police LV";
        case 599: return "Police Ranger";
        case 600: return "Picador";
        case 601: return "S.W.A.T.";
        case 602: return "Alpha";
        case 603: return "Phoenix";
        default: return "";
        }
    }
} // namespace VantaMP::Shared
