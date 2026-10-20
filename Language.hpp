#pragma once
#include <map>
#include <string>
inline std::map<std::string,std::string> languageDefaults(bool en) {
 const std::string tag="[i:4076][c/d62ef0:"+std::string(en?"Dimensions":"Измерения")+"] ";
 return {
 {"nameAlreadyOnServer",en?"Player ${name} is already on the server.":"Игрок с именем ${name} уже находится на сервере."},
 {"characterNameLengthOutOfRange",en?"Character name must contain 2 to 20 characters.":"Имя персонажа должно содержать от 2 до 20 символов включительно."},
 {"areYouEvenConnected",tag+(en?"Are you connected?":"Вы вообще подключены?")},
 {"youAreAlreadyInthatDimension",tag+(en?"[c/f88efa:You are already in this dimension.]":"[c/f88efa:Вы уже пребываете в данном измерении.]")},
 {"shiftingToDimension",tag+(en?"[c/8efad2:Shifting to dimension] ${name}...":"[c/8efad2:Перемещаем вас в новое измерение] ${name}...")},
 {"youNeedToWaitUntilConnected",tag+(en?"Please wait for the current connection to complete.":"Дождитесь полного подключения к текущему Измерению.")},
 {"playerCount",tag+(en?"[c/f88efa:${total} players online.]":"[c/f88efa:Сейчас на серверах ${total} игроков.]")},
 {"availableDimensions",tag+(en?"[c/f88efa:Available dimensions:]":"[c/f88efa:Доступные измерения:]")},
 {"youEnteredTheVoid",tag+(en?"[c/f88efa:You entered the Void. You will disappear soon.]":"[c/f88efa:Вы вошли в Пустоту. Скоро вы исчезнете.]")},
 {"dimensionDisconnectedYou",tag+(en?"disconnected you.":"отключило вас.")},
 {"reason"," "},{"dimensionsCommandName","dim"},
 {"specifyADimensionToTravel",en?"Specify a dimension to travel to:":"Укажите [c/d62ef0:Измерение], в которое хотите отправиться:"},
 {"dimensionDropped",en?"Connection to your dimension was lost.":"Измерение, в котором вы были, потеряло связь."},
 {"blacklisted",en?"You are blacklisted.":"Вы занесены в черный список сервера."},
 {"blacklistCheckError",en?"Blacklist check failed.":"Ошибка при проверке вашего статуса в черном списке."},
 {"invalidPacketLength",en?"Invalid client packet length.":"Нарушение протокола клиента: неверная длина пакета."},
 {"close",en?"Server restarting. Please reconnect.":"Сервер перезагружается. Пожалуйста, перезайдите."},
 {"connectionRateLimitExceeded",en?"Too many connections. Please wait.":"Слишком много попыток подключения. Пожалуйста, подождите и попробуйте снова."},
 {"noServersAvailable",en?"Servers are unavailable. Please try again later.":"Серверы сейчас недоступны. Пожалуйста, попробуйте позже."},
 {"currentTcpRtt",tag+(en?"Your network ping: ${overallRtt}":"Ваш пинг в сети: ${overallRtt}")},
 {"tcpRttUnavailable",tag+(en?"[c/ff3333:Error:] TCP RTT is unavailable.":"[c/ff3333:Ошибка:] задержку (RTT) сейчас измерить не удалось.")}
 };
}
