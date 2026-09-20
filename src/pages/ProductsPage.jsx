import React, { useState } from 'react';
import Header from '../components/Header';
import SiteFooter from '../components/SiteFooter';
import { useParams, useNavigate } from 'react-router-dom';
import { API_BASE } from '../api.js';
import './ProductsPage.css';

const PRODUCT_INFO = {
  external: {
    name: 'EXTERNAL',
    logo: '/external.png',
    status: 'Работает',
    description: 'Внешняя модификация для GTA V. Функционирует полностью извне игрового процесса, не требует инжекции в память. Обеспечивает стабильную и безопасную работу на любых серверах, не нарушая целостность клиента.',
    price: '500',
    currency: '₽',
    usdtPrice: '4.20',
    releaseDate: '24.11.2019',
    updateDate: '20.04.2026',
    systemRequirements: {
      os: 'Windows 10-11',
      version: 'GTA V Legacy',
      support: 'x64, only origin, 20H2+'
    },
    anticheats: [
      { name: 'Easy Anti-Cheat', status: 'Undetected' },
      { name: 'Majestic Anti-Cheat', status: 'Undetected' },
      { name: 'GTA5RP Anti Cheat', status: 'Undetected' }
    ],
    featuresGrid: {
      'AIMBOT': ['Enable Aimbot', 'Aim Type', 'Draw FOV', 'Hit Chance', 'Aim Bone', 'Smoothing', 'Ignore Friends'],
      'TRIGGER': ['Enable Triggerbot', 'Trigger Key', 'Trigger Delay', 'Ignore Friends'],
      'VISUALS': ['ESP Box', 'Skeleton', 'Distance', 'Weapon', 'Health Bar', 'Player Name', 'Aim Line', 'Custom Colors'],
      'OBJECTS': ['Objects ESP', 'Objects Distance'],
      'WEAPONS': ['No Recoil (detected)', 'No Spread (detected)', 'Wallshot (detected)', 'Double Tap (detected)'],
      'OTHER': ['Bypass OBS', 'Config System', 'Cheat Language', 'Suicide Key']
    },
    hasSilentOption: false,
    video: null,
    screenshots: ['/obzor1.png', '/obzor2.png', '/obzor3.png', '/obzor4.png']
  },
  trigger: {
    name: 'TRIGGER',
    logo: '/external.png',
    status: 'Работает',
    description: 'Триггербот для GTA V с опциональным модулем Silent Aim. Работает через kernel-driver без инжекции в игру. Базовая версия включает триггербот с настраиваемым бинд-кнопкой, задержкой и кнопкой паники. С модулем Silent — дополнительно FOV прицеливания и выбор хитбокса.',
    price: '500',
    silentPrice: '800',
    currency: '₽',
    usdtPrice: '6.00',
    usdtSilentPrice: '9.60',
    releaseDate: '01.06.2025',
    updateDate: '20.04.2026',
    systemRequirements: {
      os: 'Windows 10-11',
      version: 'GTA V Legacy',
      support: 'x64, only origin, 20H2+'
    },
    anticheats: [
      { name: 'Easy Anti-Cheat', status: 'Undetected' },
      { name: 'Majestic Anti-Cheat', status: 'Undetected' },
      { name: 'GTA5RP Anti Cheat', status: 'Undetected' }
    ],
    featuresGrid: {
      'TRIGGER': ['Trigger Key Bind', 'Trigger Delay (0-5ms)', 'Panic Button'],
      'SILENT (addon)': ['Silent Bind Key', 'FOV (0-360°)', 'Hitbox Select', 'Nearest Target']
    },
    hasSilentOption: true,
    video: null,
    screenshots: ['/ragemp-screen.png']
  },
  ragemp: {
    name: 'RAGE:MP',
    logo: '/external.png',
    status: 'Undetected',
    description: 'Внутренняя модификация для GTA V (RAGE:MP). Функционирует внутри игрового процесса, обеспечивая стабильную работу и широкие возможности. Обходит античит системы на уровне ядра через kernel driver.',
    price: '499',
    currency: '₽',
    usdtPrice: '4.20',
    releaseDate: 'Скоро',
    updateDate: '21.08.2026',
    comingSoon: true,
    systemRequirements: {
      os: 'Windows 10-11',
      version: 'GTA V Legacy (RAGE:MP)',
      support: 'x64, only origin, 20H2+'
    },
    anticheats: [
      { name: 'Easy Anti-Cheat', status: 'Undetected' },
      { name: 'Majestic Anti-Cheat', status: 'Undetected' },
      { name: 'GTA5RP Anti Cheat', status: 'Undetected' }
    ],
    featuresGrid: {
      'AIMBOT': ['Enable Aimbot', 'Aim Type', 'Draw FOV', 'Hit Chance', 'Aim Bone', 'Smoothing', 'Ignore Friends'],
      'TRIGGER': ['Enable Triggerbot', 'Trigger Key', 'Trigger Delay', 'Ignore Friends'],
      'VISUALS': ['ESP Box', 'Skeleton', 'Distance', 'Weapon', 'Health Bar', 'Player Name', 'Aim Line', 'Custom Colors'],
      'OBJECTS': ['Objects ESP', 'Objects Distance'],
      'WEAPONS': ['No Recoil', 'No Spread', 'Wallshot', 'Double Tap'],
      'OTHER': ['Bypass OBS', 'Config System', 'Script System', 'Cheat Language', 'Suicide Key']
    },
    hasSilentOption: false,
    video: null,
    screenshots: ['/obzor1.png', '/obzor2.png', '/obzor3.png', '/obzor4.png']
  }
};

const MORE_GAMES = [
  { id: 'rdr2', title: 'RDR 2', price: 'От 330 RUB', status: 'АКТИВНЫЕ', img: 'https://via.placeholder.com/400x200/111/333?text=RDR+2' },
  { id: 'gta5', title: 'GTA V (Enhanced)', price: 'От 500 RUB', status: 'ТЕСТИРОВАНИЕ', statusColor: '#eab308', img: 'https://via.placeholder.com/400x200/111/333?text=GTA+V' },
  { id: 'redm', title: 'RedM', price: 'От 500 RUB', status: 'АКТИВНЫЕ', img: 'https://via.placeholder.com/400x200/111/333?text=RedM' },
];

export default function ProductsPage() {
  const { game } = useParams();
  const navigate = useNavigate();
  const product = PRODUCT_INFO[game];

  // Цены по продуктам и планам
  const PRICES = {
    external:  { '7d': 500,  '14d': 800,  '30d': 1200 },
    trigger:   { '7d': 300,  '14d': 400,  '30d': 500  },
    ragemp:    { '7d': 499,  '30d': 999,  '90d': 1999 },
  };
  const SILENT_ADDON = 300; // всегда +300 если включен сайлент

  const plans = [
    { id: '7d',  label: '7 дней',  baseDays: 7,  bonusDays: 1 },
    { id: '14d', label: '14 дней', baseDays: 14, bonusDays: 2 },
    { id: '30d', label: '30 дней', baseDays: 30, bonusDays: 3 },
  ];

  const [selectedPlanId, setSelectedPlanId] = useState('7d');
  const [promoCode, setPromoCode] = useState('');
  const [promoActive, setPromoActive] = useState(false);
  const [activeScreenshot, setActiveScreenshot] = useState(null);
  const [currentSlide, setCurrentSlide] = useState(0);
  const [balance, setBalance] = useState(0);
  const [silentEnabled, setSilentEnabled] = useState(false);

  React.useEffect(() => {
    const token = localStorage.getItem('token');
    if (token) {
      fetch(`${API_BASE}/api/user/me`, {
        headers: { 'Authorization': `Bearer ${token}` }
      })
        .then(res => res.json())
        .then(data => {
          if (data && data.balance !== undefined) {
            setBalance(data.balance);
          }
        })
        .catch(e => console.log(e));
    }
  }, []);

  const selectedPlan = plans.find(p => p.id === selectedPlanId);
  const selectedDays = promoActive ? selectedPlan.baseDays + selectedPlan.bonusDays : selectedPlan.baseDays;

  // Итоговая цена: берём из таблицы по продукту и плану + silent если включен
  const getDisplayPrice = () => {
    const productKey = (game === 'trigger' || game === 'external') ? game : 'external';
    const base = (PRICES[productKey] || PRICES.external)[selectedPlanId] || 0;
    const silent = (game === 'trigger' && silentEnabled) ? SILENT_ADDON : 0;
    return base + silent;
  };

  const handleApplyPromo = async () => {
    if (!promoCode) return;
    const token = localStorage.getItem('token');
    if (!token) return alert('Для активации промокода нужно войти в аккаунт');
    try {
      const res = await fetch(`${API_BASE}/api/user/me/promo`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json', 'Authorization': `Bearer ${token}` },
        body: JSON.stringify({ code: promoCode })
      });
      const data = await res.json();
      if (data.ok) {
        setPromoActive(true);
        alert('Промокод успешно применен! Дни добавлены к подписке.');
      } else {
        alert(data.error || 'Ошибка промокода');
      }
    } catch (e) { alert('Ошибка сервера'); }
  };

  const handleBuy = async () => {
    const token = localStorage.getItem('token');
    if (!token) {
      alert('Пожалуйста, войдите в аккаунт');
      return;
    }
    try {
      const productIdToBuy = (game === 'trigger' && silentEnabled) ? 'trigger_silent' : (game || 'external');
      const res = await fetch(`${API_BASE}/api/user/me/buy`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json', 'Authorization': `Bearer ${token}` },
        body: JSON.stringify({
          product_id: productIdToBuy.toLowerCase(),
          days: selectedDays,
          price: Number(getDisplayPrice()),
          promocode: promoActive ? promoCode : undefined
        })
      });
      const data = await res.json();
      if (data.ok) {
        alert('Успешно оплачено! Продукт добавлен в личный кабинет.');
        navigate('/home');
      } else {
        if (data.error === 'INSUFFICIENT_FUNDS') {
          alert('Недостаточно средств. Пожалуйста, пополните баланс в личном кабинете и после этого оплатите.');
          navigate('/home');
        } else {
          alert(data.error || 'Ошибка оплаты');
        }
      }
    } catch (e) {
      alert('Ошибка сервера');
    }
  };

  if (!product) {
    return (
      <div className="products-page">
        <Header />
        <div className="container">
          <h2>Продукт не найден</h2>
        </div>
      </div>
    );
  }

  return (
    <div className="products-page">
      <Header />
      <div className="container">
        <div className="product-layout">

          {/* LEFT COLUMN */}
          <div className="product-left">
            {/* Slider */}
            <div>
              <div className="product-slider-placeholder">
                {product.screenshots && product.screenshots.length > 0 ? (
                  <>
                    <img
                      src={product.screenshots[currentSlide]}
                      alt={`Скриншот ${currentSlide + 1}`}
                      className="product-slide-img"
                      onClick={() => setActiveScreenshot(product.screenshots[currentSlide])}
                    />

                    {/* Left Arrow */}
                    {product.screenshots.length > 1 && (
                      <button
                        type="button"
                        className="slider-arrow slider-arrow--prev"
                        aria-label="Предыдущий кадр"
                        onClick={() => setCurrentSlide((prev) => (prev > 0 ? prev - 1 : product.screenshots.length - 1))}
                      >
                        <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d="M15 18l-6-6 6-6" /></svg>
                      </button>
                    )}

                    {/* Right Arrow */}
                    {product.screenshots.length > 1 && (
                      <button
                        type="button"
                        className="slider-arrow slider-arrow--next"
                        aria-label="Следующий кадр"
                        onClick={() => setCurrentSlide((prev) => (prev < product.screenshots.length - 1 ? prev + 1 : 0))}
                      >
                        <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d="M9 18l6-6-6-6" /></svg>
                      </button>
                    )}

                    {/* Indicators */}
                    <div className="slider-dots">
                      {product.screenshots.map((_, idx) => (
                        <button
                          type="button"
                          key={idx}
                          className={`slider-dot ${currentSlide === idx ? 'is-active' : ''}`}
                          aria-label={`Кадр ${idx + 1}`}
                          onClick={() => setCurrentSlide(idx)}
                        />
                      ))}
                    </div>
                  </>
                ) : (
                  <span style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', height: '100%', color: 'white' }}>МЕСТО ДЛЯ СКРИНШОТА</span>
                )}
              </div>
            </div>

            {/* Description */}
            <div className="product-section">
              <div className="section-title">Описание</div>
              <p className="description-text">{product.description}</p>
            </div>
          </div>

          {/* RIGHT COLUMN */}
          <div className="product-right">

            <div className="product-heading-row">
              <img
                src={product.logo}
                alt=""
                className="product-heading-logo"
              />
              <h1 className="product-main-title">{product.name}</h1>
            </div>

            {/* Subscription */}
            <div className="right-card subscription-card">

              {/* Silent toggle — только для trigger */}
              {product.hasSilentOption && (
                <button
                  type="button"
                  className={`silent-toggle ${silentEnabled ? 'is-on' : ''}`}
                  onClick={() => setSilentEnabled(v => !v)}
                >
                  <span className="silent-check" aria-hidden="true">
                    {silentEnabled && (
                      <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round">
                        <polyline points="20 6 9 17 4 12" />
                      </svg>
                    )}
                  </span>
                  <span className="silent-copy">
                    <span className="silent-title">Silent Aim</span>
                    <span className="silent-hint">
                      {silentEnabled ? 'FOV, хитбокс, бинд — +300₽' : 'Добавить модуль Silent Aim +300₽'}
                    </span>
                  </span>
                  <span className="silent-price tnum">
                    {silentEnabled
                      ? `${(PRICES.trigger[selectedPlanId] || 0) + SILENT_ADDON}₽`
                      : `${PRICES.trigger[selectedPlanId] || 0}₽`}
                  </span>
                </button>
              )}

              <div className="plan-selector-grid" style={{ marginBottom: '12px' }}>
                {plans.map(plan => {
                  const totalDays = promoActive ? plan.baseDays + plan.bonusDays : plan.baseDays;
                  return (
                    <button
                      key={plan.id}
                      className={`plan-btn ${selectedPlanId === plan.id ? 'active' : ''}`}
                      onClick={() => setSelectedPlanId(plan.id)}
                    >
                      <div className="plan-btn-days">{totalDays} {totalDays % 10 === 1 && totalDays % 100 !== 11 ? 'день' : [2, 3, 4].includes(totalDays % 10) && ![12, 13, 14].includes(totalDays % 100) ? 'дня' : 'дней'}</div>
                    </button>
                  );
                })}
              </div>

              <div className="coupon-box" style={{ marginBottom: '16px' }}>
                <input type="text" className="coupon-input" placeholder="Введите промокод" value={promoCode} onChange={e => setPromoCode(e.target.value)} />
                <button className="coupon-btn" onClick={handleApplyPromo}>Применить</button>
              </div>

              <div className="price-rows">
                <div className="price-row">
                  <span>Дни за реф-код</span>
                  <span className="tnum">{promoActive ? `+${selectedPlan.bonusDays}` : '0'}</span>
                </div>
                <div className="price-row">
                  <span>Итоговая цена</span>
                  <span className="tnum">
                    {getDisplayPrice ? getDisplayPrice() : selectedPlan.price} {product.currency}
                    {game === 'trigger' && silentEnabled && (
                      <span className="silent-tag">+Silent</span>
                    )}
                  </span>
                </div>
              </div>

              <div className="payment-buttons">
                <button
                  type="button"
                  className="pay-btn pay-rub"
                  onClick={product.comingSoon ? undefined : handleBuy}
                  disabled={!!product.comingSoon}
                  title={product.comingSoon ? 'Скоро в продаже' : undefined}
                >
                  <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                    <polyline points="20 6 9 17 4 12"></polyline>
                  </svg>
                  Приобрести
                </button>
              </div>
              <p className="reseller-note">
                Can't make a payment from your country? Write to our <a href="https://t.me/fraud444" target="_blank" rel="noreferrer">reseller</a>
              </p>
            </div>

            <div className="product-meta">
              <div className="ac-list">
                {product.anticheats.map((ac, idx) => (
                  <div key={idx} className="ac-item">
                    <div className="ac-name">{ac.name}</div>
                    <div className="ac-status">{ac.status}</div>
                  </div>
                ))}
              </div>

              {product.systemRequirements && (
                <table className="spec-sheet tnum">
                  <tbody>
                    <tr>
                      <th>Система</th>
                      <td>{product.systemRequirements.os}</td>
                    </tr>
                    <tr>
                      <th>Версия игры</th>
                      <td>{product.systemRequirements.version}</td>
                    </tr>
                    <tr>
                      <th>Поддержка</th>
                      <td>{product.systemRequirements.support}</td>
                    </tr>
                  </tbody>
                </table>
              )}
            </div>

          </div>
        </div>


      </div>
      {/* Image Modal */}
      {activeScreenshot && (
        <div className="shot-overlay" onClick={() => setActiveScreenshot(null)}>
          <div className="shot-overlay__inner">
            <button type="button" className="shot-overlay__close" onClick={() => setActiveScreenshot(null)} aria-label="Закрыть">
              ×
            </button>
            <img src={activeScreenshot} alt="Скриншот продукта" />
          </div>
        </div>
      )}

      <SiteFooter />

    </div>
  );
}

