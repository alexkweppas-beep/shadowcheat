import React, { useState } from 'react';
import Header from '../components/Header';
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
        <div className="product-layout" style={{ marginTop: '32px' }}>

          {/* LEFT COLUMN */}
          <div className="product-left">
            {/* Slider */}
            <div>
              <div
                className="product-slider-placeholder"
                style={{ padding: 0, overflow: 'hidden', background: '#111', position: 'relative', borderRadius: '12px', aspectRatio: '16/9' }}
              >
                {product.screenshots && product.screenshots.length > 0 ? (
                  <>
                    <img
                      src={product.screenshots[currentSlide]}
                      alt={`Screenshot ${currentSlide + 1}`}
                      style={{ width: '100%', height: '100%', objectFit: 'cover', cursor: 'pointer' }}
                      onClick={() => setActiveScreenshot(product.screenshots[currentSlide])}
                    />

                    {/* Left Arrow */}
                    {product.screenshots.length > 1 && (
                      <button
                        onClick={() => setCurrentSlide((prev) => (prev > 0 ? prev - 1 : product.screenshots.length - 1))}
                        style={{ position: 'absolute', top: '50%', left: '16px', transform: 'translateY(-50%)', background: 'rgba(0,0,0,0.5)', color: 'white', border: 'none', borderRadius: '50%', width: '40px', height: '40px', display: 'flex', alignItems: 'center', justifyContent: 'center', cursor: 'pointer', transition: 'background 0.2s' }}
                        onMouseOver={e => e.currentTarget.style.background = 'rgba(0,0,0,0.8)'}
                        onMouseOut={e => e.currentTarget.style.background = 'rgba(0,0,0,0.5)'}
                      >
                        <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d="M15 18l-6-6 6-6" /></svg>
                      </button>
                    )}

                    {/* Right Arrow */}
                    {product.screenshots.length > 1 && (
                      <button
                        onClick={() => setCurrentSlide((prev) => (prev < product.screenshots.length - 1 ? prev + 1 : 0))}
                        style={{ position: 'absolute', top: '50%', right: '16px', transform: 'translateY(-50%)', background: 'rgba(0,0,0,0.5)', color: 'white', border: 'none', borderRadius: '50%', width: '40px', height: '40px', display: 'flex', alignItems: 'center', justifyContent: 'center', cursor: 'pointer', transition: 'background 0.2s' }}
                        onMouseOver={e => e.currentTarget.style.background = 'rgba(0,0,0,0.8)'}
                        onMouseOut={e => e.currentTarget.style.background = 'rgba(0,0,0,0.5)'}
                      >
                        <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d="M9 18l6-6-6-6" /></svg>
                      </button>
                    )}

                    {/* Indicators */}
                    <div style={{ position: 'absolute', bottom: '16px', left: '50%', transform: 'translateX(-50%)', display: 'flex', gap: '8px' }}>
                      {product.screenshots.map((_, idx) => (
                        <div key={idx} onClick={() => setCurrentSlide(idx)} style={{ width: '8px', height: '8px', borderRadius: '50%', background: currentSlide === idx ? 'white' : 'rgba(255,255,255,0.4)', cursor: 'pointer', transition: 'background 0.2s' }} />
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

            <div style={{ marginBottom: '8px' }}>
              <div style={{ display: 'flex', alignItems: 'center', gap: '28px' }}>
                <img
                  src={product.logo}
                  alt="Logo"
                  style={{
                    width: '48px',
                    height: '48px',
                    objectFit: 'contain',
                    transform: 'scale(1.6)'
                  }}
                />
                <h1 className="product-main-title" style={{ margin: 0 }}>{product.name}</h1>
              </div>
            </div>

            {/* Subscription */}
            <div className="right-card subscription-card">

              {/* Silent toggle — только для trigger */}
              {product.hasSilentOption && (
                <div style={{
                  display: 'flex', alignItems: 'center', gap: '12px',
                  marginBottom: '16px', padding: '12px 14px',
                  background: silentEnabled ? 'rgba(74,222,128,0.08)' : 'rgba(255,255,255,0.03)',
                  border: silentEnabled ? '1px solid rgba(74,222,128,0.3)' : '1px solid rgba(255,255,255,0.06)',
                  borderRadius: '10px', cursor: 'pointer', transition: 'all 0.2s'
                }} onClick={() => setSilentEnabled(v => !v)}>
                  <div style={{
                    width: '20px', height: '20px', borderRadius: '5px',
                    border: silentEnabled ? '2px solid #4ade80' : '2px solid rgba(255,255,255,0.2)',
                    background: silentEnabled ? '#4ade80' : 'transparent',
                    display: 'flex', alignItems: 'center', justifyContent: 'center',
                    transition: 'all 0.2s', flexShrink: 0
                  }}>
                    {silentEnabled && (
                      <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="#000" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round">
                        <polyline points="20 6 9 17 4 12" />
                      </svg>
                    )}
                  </div>
                  <div style={{ flex: 1 }}>
                    <div style={{ fontSize: '14px', fontWeight: '600', color: silentEnabled ? '#4ade80' : 'var(--text-primary)' }}>
                      Silent Aim
                    </div>
                    <div style={{ fontSize: '12px', color: 'var(--text-secondary)', marginTop: '2px' }}>
                      {silentEnabled ? `FOV, хитбокс, бинд — +300₽` : 'Добавить модуль Silent Aim +300₽'}
                    </div>
                  </div>
                  <div style={{
                    fontSize: '14px', fontWeight: '700',
                    color: silentEnabled ? '#4ade80' : 'var(--text-secondary)'
                  }}>
                    {silentEnabled
                      ? `${(PRICES.trigger[selectedPlanId] || 0) + SILENT_ADDON}₽`
                      : `${PRICES.trigger[selectedPlanId] || 0}₽`}
                  </div>
                </div>
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

              <div style={{ display: 'flex', flexDirection: 'column', gap: '12px', marginBottom: '24px' }}>
                <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: '15px' }}>
                  <span style={{ color: 'var(--text-secondary)' }}>Дни за реф-код</span>
                  <span style={{ fontWeight: '600' }}>{promoActive ? `+${selectedPlan.bonusDays} days` : '0'}</span>
                </div>
                <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: '15px' }}>
                  <span style={{ color: 'var(--text-secondary)' }}>Итоговая цена</span>
                  <span style={{ fontWeight: '600' }}>
                    {getDisplayPrice ? getDisplayPrice() : selectedPlan.price} {product.currency}
                    {game === 'trigger' && silentEnabled && (
                      <span style={{ fontSize: '11px', color: 'rgba(74,222,128,0.8)', marginLeft: '6px' }}>+Silent</span>
                    )}
                  </span>
                </div>
              </div>

              <div className="payment-buttons">
                <button className="pay-btn pay-rub" style={{ width: '100%' }} onClick={handleBuy}>
                  <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                    <polyline points="20 6 9 17 4 12"></polyline>
                  </svg>
                  Приобрести
                </button>
              </div>
              <div style={{ textAlign: 'center', marginTop: '12px', fontSize: '10px', opacity: 0.4 }}>
                Can't make a payment from your country? Write to our <a href="https://t.me/fraud444" target="_blank" rel="noreferrer" style={{ color: 'inherit', textDecoration: 'underline' }}>reseller</a>
              </div>
            </div>

            <div style={{ marginTop: '-8px' }}>
              <div className="ac-list" style={{ marginBottom: '8px' }}>
                {product.anticheats.map((ac, idx) => (
                  <div key={idx} className="ac-item">
                    <div className="ac-name">
                      {ac.name}
                    </div>
                    <div className="ac-status">{ac.status}</div>
                  </div>
                ))}
              </div>

              {product.systemRequirements && (
                <div style={{
                  background: 'rgba(255, 255, 255, 0.03)',
                  backdropFilter: 'blur(10px)',
                  border: '1px solid rgba(255, 255, 255, 0.05)',
                  borderRadius: '12px',
                  padding: '16px',
                  display: 'flex',
                  flexDirection: 'column',
                  gap: '12px'
                }}>
                  <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: '14px' }}>
                    <span style={{ color: 'var(--text-secondary)' }}>Система</span>
                    <span style={{ fontWeight: '500' }}>{product.systemRequirements.os}</span>
                  </div>
                  <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: '14px' }}>
                    <span style={{ color: 'var(--text-secondary)' }}>Версия игры</span>
                    <span style={{ fontWeight: '500' }}>{product.systemRequirements.version}</span>
                  </div>
                  <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: '14px' }}>
                    <span style={{ color: 'var(--text-secondary)' }}>Поддержка</span>
                    <span style={{ fontWeight: '500' }}>{product.systemRequirements.support}</span>
                  </div>
                </div>
              )}
            </div>

          </div>
        </div>


      </div>
      {/* Image Modal */}
      {activeScreenshot && (
        <div
          style={{
            position: 'fixed', top: 0, left: 0, right: 0, bottom: 0,
            background: 'rgba(0,0,0,0.9)', zIndex: 9999,
            display: 'flex', alignItems: 'center', justifyContent: 'center', padding: '40px'
          }}
          onClick={() => setActiveScreenshot(null)}
        >
          <div style={{ position: 'relative', maxWidth: '90vw', maxHeight: '90vh' }}>
            <button
              onClick={() => setActiveScreenshot(null)}
              style={{
                position: 'absolute', top: '-40px', right: 0, background: 'none', border: 'none',
                color: '#fff', fontSize: '32px', cursor: 'pointer'
              }}
            >
              &times;
            </button>
            <img
              src={activeScreenshot}
              alt="Fullscreen Screenshot"
              style={{ width: '100%', height: '100%', objectFit: 'contain', borderRadius: '12px', maxHeight: '85vh' }}
            />
          </div>
        </div>
      )}

      {/* Footer */}
      <footer style={{
        borderTop: '1px solid rgba(255,255,255,0.05)',
        padding: '32px 0',
        marginTop: '60px',
        color: 'rgba(255,255,255,0.5)',
        fontSize: '14px'
      }}>
        <div className="container" style={{
          display: 'flex',
          justifyContent: 'center',
          alignItems: 'center',
          gap: '24px',
          flexWrap: 'wrap'
        }}>
          <div>© 2026 shadow. Все права защищены.</div>
          <div style={{ width: '1px', height: '16px', background: 'rgba(255,255,255,0.1)' }}></div>
          <div style={{ display: 'flex', gap: '16px' }}>
            <a href="#" style={{ color: 'inherit', textDecoration: 'none', transition: 'color 0.2s' }} onMouseOver={e => e.target.style.color = 'white'} onMouseOut={e => e.target.style.color = 'rgba(255,255,255,0.5)'}>Пользовательское соглашение</a>
            <a href="#" style={{ color: 'inherit', textDecoration: 'none', transition: 'color 0.2s' }} onMouseOver={e => e.target.style.color = 'white'} onMouseOut={e => e.target.style.color = 'rgba(255,255,255,0.5)'}>Политика конфиденциальности</a>
          </div>
          <div style={{ width: '1px', height: '16px', background: 'rgba(255,255,255,0.1)' }}></div>
          <div style={{ display: 'flex', gap: '12px' }}>
            <a href="#" style={{ display: 'flex', alignItems: 'center', gap: '6px', background: 'rgba(255,255,255,0.05)', padding: '6px 12px', borderRadius: '6px', color: 'inherit', textDecoration: 'none', transition: 'background 0.2s' }} onMouseOver={e => e.currentTarget.style.background = 'rgba(255,255,255,0.1)'} onMouseOut={e => e.currentTarget.style.background = 'rgba(255,255,255,0.05)'}>
              <svg width="14" height="14" viewBox="0 0 24 24" fill="currentColor"><path d="M20.317 4.3698a19.7913 19.7913 0 00-4.8851-1.5152.0741.0741 0 00-.0785.0371c-.211.3753-.4447.8648-.6083 1.2495-1.8447-.2762-3.68-.2762-5.4868 0-.1636-.3933-.4058-.8742-.6177-1.2495a.077.077 0 00-.0785-.037 19.7363 19.7363 0 00-4.8852 1.515.0699.0699 0 00-.0321.0277C.5334 9.0458-.319 13.5799.0992 18.0578a.0824.0824 0 00.0312.0561c2.0528 1.5076 4.0413 2.4228 5.9929 3.0294a.0777.0777 0 00.0842-.0276c.4616-.6304.8731-1.2952 1.226-1.9942a.076.076 0 00-.0416-.1057c-.6528-.2476-1.2743-.5495-1.8722-.8923a.077.077 0 01-.0076-.1277c.1258-.0943.2517-.1923.3718-.2914a.0743.0743 0 01.0776-.0105c3.9278 1.7933 8.18 1.7933 12.0614 0a.0739.0739 0 01.0785.0095c.1202.099.246.1981.3728.2924a.077.077 0 01-.0066.1276 12.2986 12.2986 0 01-1.873.8914.0766.0766 0 00-.0407.1067c.3604.698.7719 1.3628 1.225 1.9932a.076.076 0 00.0842.0286c1.961-.6067 3.9495-1.5219 6.0023-3.0294a.077.077 0 00.0313-.0552c.5004-5.177-.8382-9.6739-3.5485-13.6604a.061.061 0 00-.0312-.0286zM8.02 15.3312c-1.1825 0-2.1569-1.0857-2.1569-2.419 0-1.3332.9555-2.4189 2.157-2.4189 1.2108 0 2.1757 1.0952 2.1568 2.419 0 1.3332-.9555 2.4189-2.1569 2.4189zm7.9748 0c-1.1825 0-2.1569-1.0857-2.1569-2.419 0-1.3332.9554-2.4189 2.1569-2.4189 1.2108 0 2.1757 1.0952 2.1568 2.419 0 1.3332-.946 2.4189-2.1568 2.4189Z" /></svg>
              Discord
            </a>
            <a href="#" style={{ display: 'flex', alignItems: 'center', gap: '6px', background: 'rgba(255,255,255,0.05)', padding: '6px 12px', borderRadius: '6px', color: 'inherit', textDecoration: 'none', transition: 'background 0.2s' }} onMouseOver={e => e.currentTarget.style.background = 'rgba(255,255,255,0.1)'} onMouseOut={e => e.currentTarget.style.background = 'rgba(255,255,255,0.05)'}>
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d="M22 2L11 13"></path><path d="M22 2L15 22L11 13L2 9L22 2Z"></path></svg>
              Telegram
            </a>
            <a href="#" style={{ display: 'flex', alignItems: 'center', gap: '6px', background: 'rgba(255,255,255,0.05)', padding: '6px 12px', borderRadius: '6px', color: 'inherit', textDecoration: 'none', transition: 'background 0.2s' }} onMouseOver={e => e.currentTarget.style.background = 'rgba(255,255,255,0.1)'} onMouseOut={e => e.currentTarget.style.background = 'rgba(255,255,255,0.05)'}>
              <svg width="14" height="14" viewBox="0 0 24 24" fill="currentColor" style={{ transform: 'scale(1.2)' }}><path d="M23.498 6.186a3.016 3.016 0 0 0-2.122-2.136C19.505 3.545 12 3.545 12 3.545s-7.505 0-9.377.505A3.017 3.017 0 0 0 .502 6.186C0 8.07 0 12 0 12s0 3.93.502 5.814a3.016 3.016 0 0 0 2.122 2.136c1.871.505 9.376.505 9.376.505s7.505 0 9.377-.505a3.015 3.015 0 0 0 2.122-2.136C24 15.93 24 12 24 12s0-3.93-.502-5.814zM9.545 15.568V8.432L15.818 12l-6.273 3.568z" /></svg>
              YouTube
            </a>
          </div>
        </div>
      </footer>

    </div>
  );
}
