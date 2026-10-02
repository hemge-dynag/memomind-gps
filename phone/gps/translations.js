// UI strings for the gps phone plugin. Locale follows the system
// language (navigator.language) — French is the default. Supported:
// fr, en, es, it, de, zh-CN.

const STRINGS = {
  fr: {
    appTitle: 'Course / Rando',
    statusStarting: 'Démarrage…',
    statusConnected: 'Lunettes connectées',
    statusDisconnected: 'Lunettes déconnectées',
    metricDistance: 'Distance',
    metricDuration: 'Durée',
    metricPace: 'Allure',
    metricSpeed: 'Vitesse',
    startButton: 'Démarrer',
    pauseButton: 'Pause',
    resumeButton: 'Reprendre',
    sectionStats: 'Sorties',
    statToday: "aujourd'hui",
    statWeek: '7 derniers jours',
    statWeekSessions: 'sorties (7 j)',
    noSession: 'Aucune sortie pour le moment.',
    lastSession: (distance, duration) =>
      `Dernière sortie : ${distance} en ${duration}.`,
  },
  en: {
    appTitle: 'Run / Hike',
    statusStarting: 'Starting…',
    statusConnected: 'Glasses connected',
    statusDisconnected: 'Glasses disconnected',
    metricDistance: 'Distance',
    metricDuration: 'Duration',
    metricPace: 'Pace',
    metricSpeed: 'Speed',
    startButton: 'Start',
    pauseButton: 'Pause',
    resumeButton: 'Resume',
    sectionStats: 'Activities',
    statToday: 'today',
    statWeek: 'last 7 days',
    statWeekSessions: 'activities (7 d)',
    noSession: 'No activity yet.',
    lastSession: (distance, duration) =>
      `Last activity: ${distance} in ${duration}.`,
  },
  es: {
    appTitle: 'Carrera / Senderismo',
    statusStarting: 'Iniciando…',
    statusConnected: 'Gafas conectadas',
    statusDisconnected: 'Gafas desconectadas',
    metricDistance: 'Distancia',
    metricDuration: 'Duración',
    metricPace: 'Ritmo',
    metricSpeed: 'Velocidad',
    startButton: 'Empezar',
    pauseButton: 'Pausa',
    resumeButton: 'Reanudar',
    sectionStats: 'Salidas',
    statToday: 'hoy',
    statWeek: 'últimos 7 días',
    statWeekSessions: 'salidas (7 d)',
    noSession: 'Aún no hay salidas.',
    lastSession: (distance, duration) =>
      `Última salida: ${distance} en ${duration}.`,
  },
  it: {
    appTitle: 'Corsa / Escursione',
    statusStarting: 'Avvio…',
    statusConnected: 'Occhiali connessi',
    statusDisconnected: 'Occhiali disconnessi',
    metricDistance: 'Distanza',
    metricDuration: 'Durata',
    metricPace: 'Ritmo',
    metricSpeed: 'Velocità',
    startButton: 'Inizia',
    pauseButton: 'Pausa',
    resumeButton: 'Riprendi',
    sectionStats: 'Uscite',
    statToday: 'oggi',
    statWeek: 'ultimi 7 giorni',
    statWeekSessions: 'uscite (7 g)',
    noSession: 'Nessuna uscita per ora.',
    lastSession: (distance, duration) =>
      `Ultima uscita: ${distance} in ${duration}.`,
  },
  de: {
    appTitle: 'Lauf / Wanderung',
    statusStarting: 'Startet…',
    statusConnected: 'Brille verbunden',
    statusDisconnected: 'Brille getrennt',
    metricDistance: 'Distanz',
    metricDuration: 'Dauer',
    metricPace: 'Pace',
    metricSpeed: 'Tempo',
    startButton: 'Starten',
    pauseButton: 'Pause',
    resumeButton: 'Fortsetzen',
    sectionStats: 'Aktivitäten',
    statToday: 'heute',
    statWeek: 'letzte 7 Tage',
    statWeekSessions: 'Aktivitäten (7 T)',
    noSession: 'Noch keine Aktivität.',
    lastSession: (distance, duration) =>
      `Letzte Aktivität: ${distance} in ${duration}.`,
  },
  zh: {
    appTitle: '跑步 / 徒步',
    statusStarting: '启动中…',
    statusConnected: '眼镜已连接',
    statusDisconnected: '眼镜未连接',
    metricDistance: '距离',
    metricDuration: '时长',
    metricPace: '配速',
    metricSpeed: '速度',
    startButton: '开始',
    pauseButton: '暂停',
    resumeButton: '继续',
    sectionStats: '运动记录',
    statToday: '今天',
    statWeek: '最近 7 天',
    statWeekSessions: '次数（7 天）',
    noSession: '尚无运动记录。',
    lastSession: (distance, duration) => `上次运动：${distance}，用时 ${duration}。`,
  },
};

export function detectLocale() {
  const tag = (navigator.language || 'fr').toLowerCase();
  if (tag.startsWith('en')) return 'en';
  if (tag.startsWith('es')) return 'es';
  if (tag.startsWith('it')) return 'it';
  if (tag.startsWith('de')) return 'de';
  if (tag.startsWith('zh')) return 'zh';
  return 'fr';
}

export function getStrings(locale) {
  return STRINGS[locale] || STRINGS.fr;
}
